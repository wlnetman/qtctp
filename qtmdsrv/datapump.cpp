#include "datapump.h"
#include "ringbuffer.h"
#include "ApiStruct.h"
#include "servicemgr.h"
#include "logger.h"
#include <windows.h>
#include <QLoggingCategory>
#include "ctpmgr.h"
#include <QDir>
#include <leveldb/db.h>
#include "utils.h"
#include "profile.h"

DataPump::DataPump(QObject* parent)
    : QObject(parent)
{
}

void DataPump::init()
{
    g_sm->logger()->info(__FUNCTION__);
    db_backend_ = new LevelDBBackend;
    db_thread_ = new QThread;
    db_backend_->moveToThread(db_thread_);

    QObject::connect(db_thread_, &QThread::started, db_backend_, &LevelDBBackend::init);
    QObject::connect(db_thread_, &QThread::finished, db_backend_, &LevelDBBackend::shutdown);
    QObject::connect(this, &DataPump::gotTick, db_backend_, &LevelDBBackend::putTick);
    QObject::connect(db_backend_, &LevelDBBackend::opened, this, &DataPump::initInstrumentLocator);

    db_thread_->start();
}

//guithread的eventloop退了，不会处理dbthread的finish，这里应该等待线程退出，然后清理qthread对象
//对象属于哪个线程就在哪个线程上清理=
//db_backend_自己删除自己=
void DataPump::shutdown()
{
    g_sm->logger()->info(__FUNCTION__);
    db_thread_->quit();
    db_thread_->wait();
    delete db_thread_;
    db_thread_ = nullptr;
}

//在spi线程上直接调用=
void DataPump::putTick(void* tick)
{
    int indexRb = -1;
    RingBuffer* rb = nullptr;

    if (shouldSkipTick(tick)) {
        return;
    }

    void* newTick = this->putTickToRingBuffer(tick, indexRb, rb);
    fixTickMs(newTick, indexRb, rb);

    emit this->gotTick(newTick, indexRb, rb);
}

//1.如果和前一个tick一样就不保存了（时间，最新价，成交量，持仓量，买一卖一价，买一卖一申报量）
//白糖会在每次开盘时候，先发一个上次的收盘tick但日期是不一样的，晕。如23号早上9:00:00会
//收到一个22号的23:29:59的tick但日期确实23号=
//2.如果时间无效不保存，如有效区间[09:15:00-15:30:00)[21:00:00-23:30:00) [00:00:00-02:30:00)
//3.todo(sunwangme):IH在9:15:00时候出现了买一卖一价非常庞大应该是没有初始化的问题，需要处理=
// 2&3交给客户端去做更合理，mdsrv只负责收原始数据=
bool DataPump::shouldSkipTick(void *tick){
    if (0){
        char* timeTick = ((DepthMarketDataField*)tick)->UpdateTime;
        bool valid = false;

        // 金融期货 IF 中金所=
        if (memcmp(timeTick, "09:15:00", 8) >= 0 && memcmp(timeTick, "15:30:00", 8) < 0) {
            valid = true;
        };

        // 商品期货 SR 郑商所=
        if (memcmp(timeTick, "09:00:00", 8) >= 0 && memcmp(timeTick, "15:00:00", 8) < 0) {
            valid = true;
        };
        if (memcmp(timeTick, "21:00:00", 8) >= 0 && memcmp(timeTick, "23:30:00", 8) < 0) {
            valid = true;
        };

        // 商品期货 AG 上期所=
        if (memcmp(timeTick, "09:00:00", 8) >= 0 && memcmp(timeTick, "15:00:00", 8) < 0) {
            valid = true;
        };
        if (memcmp(timeTick, "21:00:00", 8) >= 0 && memcmp(timeTick, "23:59:59", 8) <= 0) {
            valid = true;
        };
        if (memcmp(timeTick, "00:00:00", 8) >= 0 && memcmp(timeTick, "02:30:00", 8) < 0) {
            valid = true;
        };

        if (!valid) {
            return true;
        }
    }

    if(1){
        DepthMarketDataField* mdf = (DepthMarketDataField*)tick;
        QString id = mdf->InstrumentID;
        RingBuffer* rb = getRingBuffer(id);
        DepthMarketDataField* lastMdf = (DepthMarketDataField*)rb->get(rb->head());
        if (lastMdf &&
                (memcmp(mdf->UpdateTime, lastMdf->UpdateTime, sizeof(mdf->UpdateTime) - 1) == 0) &&
                (mdf->LastPrice == lastMdf->LastPrice) &&
                (mdf->Volume == lastMdf->Volume) &&
                (mdf->OpenInterest == lastMdf->OpenInterest) &&
                (mdf->BidPrice1 == lastMdf->BidPrice1) &&
                (mdf->BidVolume1 == lastMdf->BidVolume1) &&
                (mdf->AskPrice1 == lastMdf->AskPrice1) &&
                (mdf->AskVolume1 == lastMdf->AskVolume1)) {
            return true;
        }
    }

    return false;
}

void DataPump::putInstrument(void* pInstrument)
{
    InstrumentField* instrument = (InstrumentField*)pInstrument;
    QString id = instrument->InstrumentID;
    leveldb::DB* db = getLevelDB();

    QString key;
    leveldb::Slice val((const char*)instrument, sizeof(InstrumentField));
    leveldb::WriteOptions options;
    key = QStringLiteral("instrument-") + id;
    db->Put(options, key.toStdString(), val);

    initTickLocator(id);
}

void DataPump::initRingBuffer(QStringList ids)
{
    if (rbs_.count() != 0) {
        qFatal("rbs_.count() != 0");
    }

    for (auto id : ids) {
        RingBuffer* rb = new RingBuffer;
        rb->init(sizeof(DepthMarketDataField), ringBufferLen_);
        rbs_.insert(id, rb);
    }

    loadRingBufferFromBackend(ids);
}

//修补tick的ms还是需要回读一下=
void DataPump::loadRingBufferFromBackend(QStringList ids)
{
    auto db = getLevelDB();
    for (auto id : ids) {
        auto rb = getRingBuffer(id);

        leveldb::ReadOptions options;
        options.fill_cache = false;
        leveldb::Iterator* it = db->NewIterator(options);
        if (!it) {
            qFatal("NewIterator == nullptr");
        }

        //第一个是tick-id+
        //最后一个是tick-id=
        QString key;
        key = QStringLiteral("tick-") + id + QStringLiteral("=");
        it->Seek(leveldb::Slice(key.toStdString()));
        if (it->Valid()) {
            it->Prev();
        }
        int count = 0;
        for (; it->Valid() && count < rb->count(); it->Prev()) {
            count++;
            if (it->value().size() != sizeof(DepthMarketDataField)) {
                qFatal("it->value().size() != sizeof(DepthMarketDataField)");
            }
            auto mdf = (DepthMarketDataField*)it->value().data();
            //遇到了前后两个结束item
            if (mdf->InstrumentID[0] == 0) {
                break;
            }
            rb->load(rb->count() - count, mdf);
        }
        delete it;
    }
}

void DataPump::freeRingBuffer()
{
    auto rb_list = rbs_.values();
    for (int i = 0; i < rb_list.length(); i++) {
        RingBuffer* rb = rb_list.at(i);
        rb->free();
        delete rb;
    }
    rbs_.clear();
}

void* DataPump::putTickToRingBuffer(void* tick, int& index, RingBuffer*& rb)
{
    DepthMarketDataField* mdf = (DepthMarketDataField*)tick;
    QString id = mdf->InstrumentID;
    rb = getRingBuffer(id);
    return rb->put(tick, index);
}

RingBuffer* DataPump::getRingBuffer(QString id)
{
    RingBuffer* rb = rbs_.value(id);
    if (rb == nullptr) {
        qFatal("rb == nullptr");
    }

    return rb;
}

leveldb::DB* DataPump::getLevelDB()
{
    return db_backend_->getLevelDB();
}

// 和前一个tick比较，如果time相同，就改ms为前一个的ms+1，不同，ms改为0
void DataPump::fixTickMs(void* tick, int indexRb, RingBuffer* rb)
{
    DepthMarketDataField* preTick = nullptr;
    DepthMarketDataField* curTick = (DepthMarketDataField*)tick;
    int index = indexRb - 1;
    if (index < 0) {
        index = index + rb->count();
    }
    preTick = (DepthMarketDataField*)rb->get(index);
    if (preTick && strcmp(curTick->UpdateTime, preTick->UpdateTime) == 0) {
        curTick->UpdateMillisec = preTick->UpdateMillisec + 1;
    }
    else {
        curTick->UpdateMillisec = 0;
    }
}

//初始化instrument定位=
void DataPump::initInstrumentLocator()
{
    leveldb::DB* db = getLevelDB();

    InstrumentField* idItem = new (InstrumentField);
    memset(idItem, 0, sizeof(InstrumentField));
    QString key;
    leveldb::Slice val((const char*)idItem, sizeof(InstrumentField));
    leveldb::WriteOptions options;
    key = QStringLiteral("instrument+");
    db->Put(options, key.toStdString(), val);
    key = QStringLiteral("instrument=");
    db->Put(options, key.toStdString(), val);
}

//初始化tick定位=
void DataPump::initTickLocator(QString id)
{
    leveldb::DB* db = getLevelDB();

    DepthMarketDataField* tick = new (DepthMarketDataField);
    memset(tick, 0, sizeof(DepthMarketDataField));
    QString key;
    leveldb::Slice val((const char*)tick, sizeof(DepthMarketDataField));
    leveldb::WriteOptions options;
    key = QStringLiteral("tick-") + id + QStringLiteral("+");
    db->Put(options, key.toStdString(), val);
    key = QStringLiteral("tick-") + id + QStringLiteral("=");
    db->Put(options, key.toStdString(), val);
}

//////
LevelDBBackend::LevelDBBackend(QObject* parent)
    : QObject(parent)
{
}

LevelDBBackend::~LevelDBBackend()
{
}

void LevelDBBackend::init()
{
    QString path = Profile::todayDbPath();
    mkDir(path);
    leveldb::Options options;
    options.create_if_missing = true;
    options.error_if_exists = false;
    options.compression = leveldb::kNoCompression;
    options.paranoid_checks = false;
    leveldb::DB* db;
    leveldb::Status status = leveldb::DB::Open(options,
        path.toStdString(),
        &db);
    if (status.ok()) {
        db_ = db;
        emit opened();
    }
    else {
        qFatal("leveldb::DB::Open fail");
    }
}

void LevelDBBackend::shutdown()
{
    delete db_;
    db_ = nullptr;

    delete this;
}

void LevelDBBackend::putTick(void* tick, int indexRb, void* rb)
{
    auto mdf = (DepthMarketDataField*)tick;
    QString id = mdf->InstrumentID;
    auto db = getLevelDB();
    QString key = QString().sprintf("tick-%s-%s-%s-%d", mdf->InstrumentID, mdf->ActionDay, mdf->UpdateTime, mdf->UpdateMillisec);
    leveldb::Slice val((const char*)tick, sizeof(DepthMarketDataField));
    leveldb::WriteOptions options;
    db->Put(options, key.toStdString(), val);
}

leveldb::DB* LevelDBBackend::getLevelDB()
{
    if (db_ == nullptr) {
        qFatal("db_ == nullptr");
    }
    return db_;
}
