#include "mdsm.h"
#include <QDir>
#include "MdApi.h"
#include "utils.h"
#include "ctpcmd.h"
#include <QMap>
#include "ringbuffer.h"
#include "servicemgr.h"
#include "ctpcmdmgr.h"
#include "logger.h"
#include "datapump.h"

///////////
class MdSmSpi : public MdSpi {
public:
    explicit MdSmSpi(MdSm* sm)
        : sm_(sm)
    {
    }

private:
    void OnFrontConnected() override
    {
        info(__FUNCTION__);
        emit sm()->statusChanged(MDSM_CONNECTED);
    }

    // 如果网络异常，会直接调用OnFrontDisconnected，需要重置状态数据=
    // 网络错误当再次恢复时候，会自动重连重新走OnFrontConnected
    void OnFrontDisconnected(int nReason) override
    {
        info(QString().sprintf("MdSmSpi::OnFrontDisconnected,nReason=0x%x", nReason));

        resetData();
        emit sm()->statusChanged(MDSM_DISCONNECTED);
    }

    // 这个spi不用被调用=（CTPSDK）
    void OnHeartBeatWarning(int nTimeLapse) override
    {
        info(__FUNCTION__);
    }

    //errorId=7，msg=CTP:还没有初始化=
    void OnRspUserLogin(RspUserLoginField* pRspUserLogin, RspInfoField* pRspInfo, int nRequestID, bool bIsLast) override
    {
        info(__FUNCTION__);
        if (bIsLast){
           if(isErrorRsp(pRspInfo, nRequestID)) {
               emit sm()->statusChanged(MDSM_LOGINFAIL);
           }else{
               emit sm()->statusChanged(MDSM_LOGINED);
           }
        }
    }

    // logout在tdapi里面是有效的,mdapi无效不用处理=
    void OnRspUserLogout(UserLogoutField* pUserLogout, RspInfoField* pRspInfo, int nRequestID, bool bIsLast) override
    {
    }

    void OnRspError(RspInfoField* pRspInfo, int nRequestID, bool bIsLast) override
    {
        info(QString().sprintf("MdSmSpi::OnRspError,reqId=%d", nRequestID));
    }

    // 订阅成功了也会调用,目前是不管啥都返回订阅成功=
    void OnRspSubMarketData(SpecificInstrumentField* pSpecificInstrument, RspInfoField* pRspInfo, int nRequestID, bool bIsLast) override
    {
        if (!isErrorRsp(pRspInfo, nRequestID) && pSpecificInstrument) {
            QString iid = pSpecificInstrument->InstrumentID;
            got_ids_ << iid;
        }

        if (bIsLast && got_ids_.length()) {
            QString ids;
            for (auto id : got_ids_) {
                ids = ids + id + ";";
            }
            info(QString().sprintf("total sub ids:%d,%s", got_ids_.length(), ids.toUtf8().constData()));
        }
    }

    void OnRspUnSubMarketData(SpecificInstrumentField* pSpecificInstrument, RspInfoField* pRspInfo, int nRequestID, bool bIsLast) override
    {
    }

    void OnRtnDepthMarketData(DepthMarketDataField* pDepthMarketData) override
    {
        g_sm->dataPump()->putTick(pDepthMarketData);
    }

private:
    bool isErrorRsp(RspInfoField* pRspInfo, int reqId)
    {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            info(QString().sprintf("<==错误，reqid=%d,errorId=%d，msg=%s",
                reqId,
                pRspInfo->ErrorID,
                gbk2utf16(pRspInfo->ErrorMsg).toUtf8().constData()));
            return true;
        }
        return false;
    }

    MdSm* sm()
    {
        return sm_;
    }

    void resetData()
    {
        got_ids_.clear();
    }

    void info(QString msg)
    {
        g_sm->logger()->info(msg);
    }

private:
    MdSm* sm_;
    QStringList got_ids_;
};

///////////
MdSm::MdSm(QObject* parent)
    : QObject(parent)
{
}

MdSm::~MdSm()
{
}

bool MdSm::init(QString userId, QString password, QString brokerId, QString frontMd, QString flowPathMd)
{
    userId_ = userId;
    password_ = password;
    brokerId_ = brokerId;
    frontMd_ = frontMd;
    flowPathMd_ = flowPathMd;

    //check
    if (userId_.length() == 0 || password_.length() == 0
        || brokerId_.length() == 0 || frontMd_.length() == 0 || flowPathMd_.length() == 0) {
        return false;
    }
    return true;
}

void MdSm::start()
{
    info(__FUNCTION__);

    if (mdapi_ != nullptr) {
        qFatal("mdapi_!=nullptr");
        return;
    }

    QDir dir;
    dir.mkpath(flowPathMd_);
    mdapi_ = MdApi::CreateMdApi(flowPathMd_.toStdString().c_str());
    g_sm->ctpCmdMgr()->setMdApi(mdapi_);
    QObject::connect(this, &MdSm::runCmd, g_sm->ctpCmdMgr(), &CtpCmdMgr::onRunCmd);
    mdspi_ = new MdSmSpi(this);
    mdapi_->RegisterSpi(mdspi_);
    mdapi_->RegisterFront((char*)qPrintable(frontMd_));
    mdapi_->Init();
    mdapi_->Join();
    g_sm->ctpCmdMgr()->setMdApi(nullptr);
    info("mdapi::join end!!!");
    emit this->statusChanged(MDSM_STOPPED);
    mdapi_ = nullptr;
    delete mdspi_;
    mdspi_ = nullptr;
}

void MdSm::stop()
{
    info(__FUNCTION__);

    if (mdapi_ == nullptr) {
        qFatal("mdapi_==nullptr");
        return;
    }

    mdapi_->RegisterSpi(nullptr);
    mdapi_->Release();
}

QString MdSm::version()
{
    return MdApi::GetApiVersion();
}

void MdSm::info(QString msg)
{
    g_sm->logger()->info(msg);
}

void MdSm::login(unsigned int delayTick){
    info(__FUNCTION__);
    emit this->runCmd(new CmdMdLogin(userId_,password_,brokerId_),delayTick);
}

void MdSm::subscrible(QStringList ids)
{
    info(__FUNCTION__);
    emit this->runCmd(new CmdMdSubscrible(ids),0);
}
