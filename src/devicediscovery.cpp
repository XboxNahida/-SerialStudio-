#include "devicediscovery.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSerialPortInfo>
#include <QStorageInfo>
#include <QThread>
#include <QTcpSocket>
#include <QUdpSocket>
#include <algorithm>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#endif

namespace {
bool localIpv4(quint32 ip)
{
    return (ip>>24)==10 || (ip>>20)==0xac1 || (ip>>16)==0xc0a8
        || (ip>>16)==0xa9fe || (ip>>24)==127;
}
QVariantMap networkDevice(const QString &host,quint16 port,const QString &name,const QString &detail)
{
    return {{"type","network"},{"name",name},{"address",host+":"+QString::number(port)},
        {"state","available"},{"detail",detail},{"actions",QStringList{"tcp"}},
        {"config",QVariantMap{{"host",host},{"port",port},{"mode",0}}}};
}
struct LocalInventory { QVariantList devices,interfaces; QString warning; };
LocalInventory localInventory()
{
    LocalInventory result;
    for(const auto &port:QSerialPortInfo::availablePorts()) {
        const auto description=port.description().isEmpty()?port.portName():port.portName()+" · "+port.description();
        result.devices.append(QVariantMap{{"type","serial"},{"name",description},{"address",port.portName()},
            {"state","available"},{"detail",port.systemLocation()+" · "+port.manufacturer()+" · "+port.serialNumber()},
            {"actions",QStringList{"serial"}},{"config",QVariantMap{{"port",port.portName()}}}});
    }
    for(const auto &volume:QStorageInfo::mountedVolumes()) {
        if(!volume.isValid() || !volume.isReady() || volume.rootPath().isEmpty()) continue;
        result.devices.append(QVariantMap{{"type","volume"},{"name",volume.displayName().isEmpty()?volume.rootPath():volume.displayName()},
            {"address",volume.rootPath()},{"state","available"},
            {"detail",QStringLiteral("%1 · %2 · 可用 %3 MiB · %4").arg(volume.rootPath(),QString::fromUtf8(volume.fileSystemType())).arg(volume.bytesAvailable()/1048576).arg(volume.isReadOnly()?QStringLiteral("只读"):QStringLiteral("可写"))},
            {"actions",QStringList{"files"}},
            {"config",QVariantMap{{"path",volume.rootPath()},{"readOnly",volume.isReadOnly()},{"bytesTotal",volume.bytesTotal()},{"bytesAvailable",volume.bytesAvailable()}}}});
    }
    for(const auto &adapter:QNetworkInterface::allInterfaces()) {
        const auto type=adapter.type()==QNetworkInterface::Ethernet?"ethernet":adapter.type()==QNetworkInterface::Wifi?"wifi":"other";
        const bool up=adapter.flags().testFlag(QNetworkInterface::IsUp);
        for(const auto &entry:adapter.addressEntries()) {
            if(entry.ip().protocol()!=QAbstractSocket::IPv4Protocol) continue;
            result.interfaces.append(QVariantMap{{"name",adapter.humanReadableName()},{"id",adapter.name()},
                {"address",entry.ip().toString()},{"prefixLength",entry.prefixLength()},{"type",type},{"up",up}});
        }
    }
#ifdef Q_OS_WIN
    const HDEVINFO devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(devices==INVALID_HANDLE_VALUE) result.warning=QStringLiteral("Windows USB 枚举失败：%1").arg(GetLastError());
    else {
        SP_DEVINFO_DATA data{}; data.cbSize=sizeof(data);
        for(DWORD index=0;index<4096 && SetupDiEnumDeviceInfo(devices,index,&data);++index) {
            wchar_t instance[1024]{};
            if(!SetupDiGetDeviceInstanceIdW(devices,&data,instance,1024,nullptr)) continue;
            const auto id=QString::fromWCharArray(instance);
            if(!id.startsWith("USB\\",Qt::CaseInsensitive)) continue;
            wchar_t name[1024]{};
            if(!SetupDiGetDeviceRegistryPropertyW(devices,&data,SPDRP_FRIENDLYNAME,nullptr,reinterpret_cast<PBYTE>(name),sizeof(name),nullptr))
                SetupDiGetDeviceRegistryPropertyW(devices,&data,SPDRP_DEVICEDESC,nullptr,reinterpret_cast<PBYTE>(name),sizeof(name),nullptr);
            name[1023]=L'\0';
            result.devices.append(QVariantMap{{"type","usb"},{"name",QString::fromWCharArray(name)},
                {"address",id},{"state","available"},{"detail",QStringLiteral("Windows 可见 USB/PnP 记录；不代表已授权、具有串口或可读写")},
                {"actions",QStringList()},{"config",QVariantMap{{"instanceId",id}}}});
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
#endif
    return result;
}
}
DeviceDiscovery::DeviceDiscovery(QObject *parent,bool monitor):QObject(parent),m_adb(this),m_monitor(monitor)
{
    connect(&m_adb,&AdbDiscovery::changed,this,&DeviceDiscovery::rebuild);
    m_broadcast=new QUdpSocket(this);
    if(m_broadcast->bind(QHostAddress::AnyIPv4,45454,QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint))
        connect(m_broadcast,&QUdpSocket::readyRead,this,&DeviceDiscovery::receiveAnnouncements);
    else m_status=QStringLiteral("UDP 服务发现端口 45454 无法监听：%1").arg(m_broadcast->errorString());
    if(monitor) {
        m_poll.setInterval(3000);
        connect(&m_poll,&QTimer::timeout,this,&DeviceDiscovery::refresh);
        m_poll.start(); m_adb.setEnabled(true);
        QTimer::singleShot(0,this,&DeviceDiscovery::refresh);
    }
}
DeviceDiscovery::~DeviceDiscovery() { cancelScan(); }
QVariantList DeviceDiscovery::normalizedDevices(const QVariantList &devices)
{
    QMap<QString,QVariantMap> unique;
    const QMap<QString,int> rank{{"known",0},{"offline",1},{"unauthorized",2},{"available",3},{"connected",4}};
    for(const auto &value:devices) {
        auto item=value.toMap();
        const auto type=item.value("type").toString(),address=item.value("address").toString().trimmed();
        if(type.isEmpty() || address.isEmpty()) continue;
        const auto id=type+":"+QString::fromLatin1(QCryptographicHash::hash((type+"\n"+address).toUtf8(),QCryptographicHash::Sha256).toHex());
        item["id"]=id; item["address"]=address;
        if(item.value("name").toString().isEmpty()) item["name"]=address;
        if(!rank.contains(item.value("state").toString())) item["state"]="known";
        if(!item.contains("detail")) item["detail"]=QString();
        if(!item.contains("actions")) item["actions"]=QStringList();
        if(!item.contains("config")) item["config"]=QVariantMap();
        if(!unique.contains(id) || rank.value(item["state"].toString())>=rank.value(unique[id]["state"].toString())) unique[id]=item;
    }
    QVariantList result;
    for(const auto &item:unique) result.append(item);
    return result;
}
QStringList DeviceDiscovery::subnetTargets(const QString &cidr,int portCount,QString *error)
{
    if(error) error->clear();
    const auto fail=[error](const QString &message) { if(error)*error=message; return QStringList(); };
    const auto parts=cidr.trimmed().split('/');
    if(parts.size()!=2) return fail(QStringLiteral("请输入明确 IPv4 CIDR，例如 192.168.1.0/24"));
    bool prefixOk=false,ipOk=false;
    const int prefix=parts[1].toInt(&prefixOk);
    const quint32 ip=QHostAddress(parts[0]).toIPv4Address(&ipOk);
    if(!prefixOk || !ipOk || prefix<24 || prefix>32)
        return fail(QStringLiteral("只允许 IPv4 /24 至 /32 网段"));
    const quint32 mask=0xffffffffU << (32-prefix),base=ip&mask;
    const quint32 count=quint32(1)<<(32-prefix);
    if(!localIpv4(base) || !localIpv4(base+count-1))
        return fail(QStringLiteral("只允许私有、链路本地或回环网段，不扫描公网"));
    const int hosts=prefix<31?int(count)-2:int(count);
    if(portCount<1 || qint64(hosts)*portCount>2048)
        return fail(QStringLiteral("扫描最多 2048 个连接任务，请缩小网段或减少端口"));
    QStringList result;
    for(quint32 offset=prefix<31?1:0;offset<count-(prefix<31?1:0);++offset) result.append(QHostAddress(base+offset).toString());
    return result;
}
QVariantMap DeviceDiscovery::parseAnnouncement(const QByteArray &data,const QHostAddress &sender)
{
    if(data.size()>16384 || sender.protocol()!=QAbstractSocket::IPv4Protocol || !localIpv4(sender.toIPv4Address())) return {};
    const auto object=QJsonDocument::fromJson(data).object();
    if(object.value("service").toString()!="serialstudio-board" || object.value("version").toInt()!=1) return {};
    const int port=object.value("port").toInt(-1);
    if(port<1 || port>65535) return {};
    const auto host=sender.toString(),name=object.value("name").toString().left(128);
    return networkDevice(host,quint16(port),name.isEmpty()?host:name,QStringLiteral("收到 SerialStudio v1 UDP 服务声明；尚未建立会话"));
}
void DeviceDiscovery::setSavedDevices(const QVariantList &devices)
{
    m_saved.clear();
    for(const auto &value:devices) {
        auto item=value.toMap(); item["state"]="known";
        item["detail"]=QStringLiteral("历史保存地址；尚未确认在线"); m_saved.append(item);
    }
    rebuild();
}
void DeviceDiscovery::rebuild()
{
    QVariantList all=m_saved;
    all+=m_local; all+=m_probed;
    for(const auto &value:m_adb.devices()) {
        const auto entry=value.toMap(); const auto serial=entry.value("serial").toString(),state=entry.value("state").toString();
        all.append(QVariantMap{{"type","adb"},{"address",serial},{"name",entry.value("label")},
            {"state",state=="device"?"available":state=="unauthorized"?"unauthorized":"offline"},
            {"detail",QStringLiteral("ADB 状态：%1；仅检测列表，未进入 shell").arg(state)},
            {"actions",state=="device"?QStringList{"adb"}:QStringList()},
            {"config",QVariantMap{{"serial",serial},{"adb",m_adb.resolvedPath()}}}});
    }
    const auto now=QDateTime::currentMSecsSinceEpoch();
    for(auto it=m_announced.begin();it!=m_announced.end();) {
        if(now-it.value().first>15000) it=m_announced.erase(it);
        else { all.append(it.value().second); ++it; }
    }
    const auto merged=normalizedDevices(all);
    if(m_devices!=merged) m_devices=merged;
    emit changed();
}
bool DeviceDiscovery::scanSubnet(const QString &cidr,const QVariantList &ports)
{
    if(m_scanning) { m_status=QStringLiteral("已有扫描正在运行，请先取消"); emit changed(); return false; }
    QSet<quint16> unique;
    const QVariantList requested=ports.isEmpty()?QVariantList{22,8765,1883,80,443}:ports;
    for(const auto &value:requested) {
        bool ok=false; const auto port=value.toString().toUInt(&ok);
        if(!ok || port<1 || port>65535) { m_status=QStringLiteral("端口必须是 1 至 65535 的整数"); emit changed(); return false; }
        unique.insert(quint16(port));
    }
    QString error; const auto hosts=subnetTargets(cidr,unique.size(),&error);
    if(hosts.isEmpty()) { m_status=error; emit changed(); return false; }
    m_tasks.clear(); m_next=0; m_completed=0;
    QList<quint16> ordered=unique.values(); std::sort(ordered.begin(),ordered.end());
    for(const auto &host:hosts) for(auto port:ordered) m_tasks.append({host,port});
    m_probed.clear(); m_scanning=true;
    m_status=QStringLiteral("扫描 %1：0/%2；仅 TCP 连接探测").arg(cidr).arg(m_tasks.size());
    rebuild(); QTimer::singleShot(0,this,&DeviceDiscovery::pumpScan); return true;
}
void DeviceDiscovery::pumpScan()
{
    if(!m_scanning) return;
    while(m_sockets.size()<24 && m_next<m_tasks.size()) {
        const auto task=m_tasks[m_next++];
        auto *socket=new QTcpSocket(this); m_sockets.insert(socket);
        socket->setProperty("host",task.host); socket->setProperty("port",task.port);
        auto *timeout=new QTimer(socket); timeout->setSingleShot(true);
        connect(timeout,&QTimer::timeout,this,[this,socket]{finishProbe(socket,false);});
        connect(socket,&QTcpSocket::connected,this,[this,socket]{finishProbe(socket,true);});
        connect(socket,&QTcpSocket::errorOccurred,this,[this,socket]{finishProbe(socket,false);});
        timeout->start(500); socket->connectToHost(QHostAddress(task.host),task.port);
    }
}
void DeviceDiscovery::finishProbe(QTcpSocket *socket,bool available)
{
    if(!m_sockets.remove(socket)) return;
    const auto host=socket->property("host").toString(); const auto port=quint16(socket->property("port").toUInt());
    socket->disconnect(this); socket->abort(); socket->deleteLater(); ++m_completed;
    if(available) m_probed.append(networkDevice(host,port,host,QStringLiteral("TCP 端口 %1 可达；服务类型及身份尚未确认").arg(port)));
    if(m_completed==m_tasks.size()) {
        m_scanning=false; m_status=QStringLiteral("扫描完成：%1 个连接任务，%2 个可达端口").arg(m_completed).arg(m_probed.size());
    } else m_status=QStringLiteral("扫描进度：%1/%2，%3 个可达端口").arg(m_completed).arg(m_tasks.size()).arg(m_probed.size());
    rebuild(); QTimer::singleShot(0,this,&DeviceDiscovery::pumpScan);
}
void DeviceDiscovery::cancelScan()
{
    if(!m_scanning) return;
    m_scanning=false;
    const auto sockets=m_sockets; m_sockets.clear();
    for(auto *socket:sockets) { socket->disconnect(this); socket->abort(); delete socket; }
    m_tasks.clear(); m_status=QStringLiteral("扫描已取消；已关闭未完成的连接"); emit changed();
}
void DeviceDiscovery::refresh()
{
    if(m_refreshing) return;
    m_refreshing=true;
    if(m_monitor) m_adb.refresh();
    if(!m_scanning) m_status=QStringLiteral("正在后台读取串口、USB、卷和本机 IPv4 网卡…");
    emit changed();
    const auto inventory=std::make_shared<LocalInventory>();
    auto *worker=QThread::create([inventory] { *inventory=localInventory(); });
    connect(worker,&QThread::finished,this,[this,inventory] {
        QVariantList updated=inventory->devices;
        QSet<QString> addresses;
        for(const auto &item:normalizedDevices(updated)) addresses.insert(item.toMap().value("id").toString());
        // 暂时消失的设备保留为离线，方便页面继续显示原参数；最多保留 1024 条旧记录。
        int missing=0;
        for(auto item:normalizedDevices(m_local)) {
            auto old=item.toMap();
            if(addresses.contains(old.value("id").toString()) || missing>=1024) continue;
            old["state"]="offline"; old["actions"]=QStringList();
            old["detail"]=QStringLiteral("本次枚举未出现；连接参数保留"); updated.append(old); ++missing;
        }
        m_local=updated; m_interfaces=inventory->interfaces; m_refreshing=false;
        if(!m_scanning) {
            m_status=QStringLiteral("本机枚举完成；服务广播仅识别 SerialStudio v1 声明，静默目标请手动扫描");
            if(!inventory->warning.isEmpty()) m_status+="；"+inventory->warning;
            if(m_broadcast->state()!=QAbstractSocket::BoundState) m_status+=QStringLiteral("；UDP 45454 监听不可用");
        }
        if(m_monitor && m_broadcast->state()==QAbstractSocket::BoundState) {
            const QByteArray request=R"({"service":"serialstudio-discover","version":1})";
            for(const auto &value:m_interfaces) {
                const auto adapter=value.toMap(); const auto ip=QHostAddress(adapter.value("address").toString()).toIPv4Address();
                const int prefix=adapter.value("prefixLength").toInt();
                if(!adapter.value("up").toBool() || !localIpv4(ip) || (ip>>24)==127 || prefix<1 || prefix>30) continue;
                const quint32 broadcast=ip | (0xffffffffU >> prefix);
                m_broadcast->writeDatagram(request,QHostAddress(broadcast),45454);
            }
        }
        rebuild();
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    worker->start();
}
void DeviceDiscovery::receiveAnnouncements()
{
    int handled=0;
    while(m_broadcast->hasPendingDatagrams() && handled++<64) {
        const auto length=m_broadcast->pendingDatagramSize();
        if(length<0) break;
        QByteArray packet(qsizetype(qMin<qint64>(length,16385)),Qt::Uninitialized);
        QHostAddress sender;
        m_broadcast->readDatagram(packet.data(),packet.size(),&sender);
        const auto item=parseAnnouncement(packet,sender);
        if(item.isEmpty()) continue;
        const auto address=item.value("address").toString();
        if(m_announced.size()<1024 || m_announced.contains(address))
            m_announced[address]={QDateTime::currentMSecsSinceEpoch(),item};
    }
    rebuild();
    if(m_broadcast->hasPendingDatagrams()) QTimer::singleShot(0,this,&DeviceDiscovery::receiveAnnouncements);
}
