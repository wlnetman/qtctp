#include "historyform.h"
#include "ui_historyform.h"
#include "servicemgr.h"
#include "datapump.h"
#include "ApiStruct.h"
#include <leveldb/db.h>
#include "QMessageBox"
#include <QDebug>

HistoryForm::HistoryForm(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::HistoryForm)
{
    ui->setupUi(this);

    //设置列=
    instruments_col_ = { "InstrumentID", "TradingDay", "UpdateTime", "UpdateMillisec",
        "LastPrice", "Volume", "OpenInterest",
        "BidPrice1", "BidVolume1", "AskPrice1", "AskVolume1" };
    this->ui->tableWidget->setColumnCount(instruments_col_.length());
    for (int i = 0; i < instruments_col_.length(); i++) {
        ui->tableWidget->setHorizontalHeaderItem(i, new QTableWidgetItem(instruments_col_.at(i)));
    }

    //设置上下分割=
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 1);

    //设置graph
    initGraph();
}

HistoryForm::~HistoryForm()
{
    delete ui;
}

void HistoryForm::Init(QString id)
{
    id_ = id;
    this->setWindowTitle(QString("history-" + id));

    on_last128_clicked();
}

void HistoryForm::on_first128_clicked()
{
    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    leveldb::ReadOptions options;
    options.fill_cache = false;
    leveldb::Iterator* it = db->NewIterator(options);
    if (!it) {
        qFatal("NewIterator == nullptr");
    }

    //第一个是tick-id+
    //最后一个是tick-id=
    QString key;
    key = QStringLiteral("tick-") + id_ + QStringLiteral("+");

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    it->Seek(leveldb::Slice(key.toStdString()));
    if (it->Valid()) {
        it->Next();
    }
    int count = 0;
    x_.clear();y_.clear();
    for (; it->Valid() && count < 128; it->Next()) {
        count++;
        if (it->value().size() != sizeof(DepthMarketDataField)) {
            qFatal("it->value().size() != sizeof(DepthMarketDataField)");
        }
        auto mdf = (DepthMarketDataField*)it->value().data();
        //遇到了前后两个结束item
        if (mdf->InstrumentID[0] == 0) {
            break;
        }
        x_.append(count);
        onGotTick(mdf);
    }
    delete it;
    drawGraph();
}

void HistoryForm::on_next128_clicked()
{
    if (ui->tableWidget->rowCount() == 0) {
        return;
    }

    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    leveldb::ReadOptions options;
    options.fill_cache = false;
    leveldb::Iterator* it = db->NewIterator(options);
    if (!it) {
        qFatal("NewIterator == nullptr");
    }

    //第一个是tick-id+
    //最后一个是tick-id=
    int r = ui->tableWidget->rowCount() - 1;
    QString d = ui->tableWidget->item(r, 1)->text();
    QString t = ui->tableWidget->item(r, 2)->text();
    QString m = ui->tableWidget->item(r, 3)->text();
    QString sep = QStringLiteral("-");
    QString key = QStringLiteral("tick-") + id_ + sep + d + sep + t + sep + m;

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    it->Seek(leveldb::Slice(key.toStdString()));
    if (it->Valid()) {
        it->Next();
    }
    int count = 0;
    x_.clear();y_.clear();
    for (; it->Valid() && count < 128; it->Next()) {
        count++;
        if (it->value().size() != sizeof(DepthMarketDataField)) {
            qFatal("it->value().size() != sizeof(DepthMarketDataField)");
        }
        auto mdf = (DepthMarketDataField*)it->value().data();
        //遇到了前后两个结束item
        if (mdf->InstrumentID[0] == 0) {
            break;
        }
        x_.append(count);
        onGotTick(mdf);
    }
    delete it;
    drawGraph();
}

void HistoryForm::on_pre128_clicked()
{
    if (ui->tableWidget->rowCount() == 0) {
        return;
    }

    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    leveldb::ReadOptions options;
    options.fill_cache = false;
    leveldb::Iterator* it = db->NewIterator(options);
    if (!it) {
        qFatal("NewIterator == nullptr");
    }

    //第一个是tick-id+
    //最后一个是tick-id=
    int r = ui->tableWidget->rowCount() - 1;
    QString d = ui->tableWidget->item(r, 1)->text();
    QString t = ui->tableWidget->item(r, 2)->text();
    QString m = ui->tableWidget->item(r, 3)->text();
    QString sep = QStringLiteral("-");
    QString key = QStringLiteral("tick-") + id_ + sep + d + sep + t + sep + m;

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    it->Seek(leveldb::Slice(key.toStdString()));
    if (it->Valid()) {
        it->Prev();
    }
    int count = 0;
    x_.clear();y_.clear();
    for (; it->Valid() && count < 128; it->Prev()) {
        count++;
        if (it->value().size() != sizeof(DepthMarketDataField)) {
            qFatal("it->value().size() != sizeof(DepthMarketDataField)");
        }
        auto mdf = (DepthMarketDataField*)it->value().data();
        //遇到了前后两个结束item
        if (mdf->InstrumentID[0] == 0) {
            break;
        }
        x_.append(count);
        onGotTick(mdf);
    }
    delete it;
    drawGraph();
}

void HistoryForm::on_last128_clicked()
{
    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    leveldb::ReadOptions options;
    options.fill_cache = false;
    leveldb::Iterator* it = db->NewIterator(options);
    if (!it) {
        qFatal("NewIterator == nullptr");
    }

    //第一个是tick-id+
    //最后一个是tick-id=
    QString key = QStringLiteral("tick-") + id_ + QStringLiteral("=");

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    it->Seek(leveldb::Slice(key.toStdString()));
    if (it->Valid()) {
        it->Prev();
    }
    int count = 0;
    x_.clear();y_.clear();
    for (; it->Valid() && count < 128; it->Prev()) {
        count++;
        if (it->value().size() != sizeof(DepthMarketDataField)) {
            qFatal("it->value().size() != sizeof(DepthMarketDataField)");
        }
        auto mdf = (DepthMarketDataField*)it->value().data();
        //遇到了前后两个结束item
        if (mdf->InstrumentID[0] == 0) {
            break;
        }
        x_.append(count);
        onGotTick(mdf);
    }
    delete it;
    drawGraph();
}

void HistoryForm::on_seekButton_clicked(){
    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    leveldb::ReadOptions options;
    options.fill_cache = false;
    leveldb::Iterator* it = db->NewIterator(options);
    if (!it) {
        qFatal("NewIterator == nullptr");
    }

    //第一个是tick-id+
    //最后一个是tick-id=
    QString key = ui->lineEdit->text();

    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    QString msg("not found,input format:\ntick-IF1511-\ntick-IF1511-20151023-1304\ntick-IF1511-20151023-1304-0");
    it->Seek(leveldb::Slice(key.toStdString()));
    if(!it->Valid()){
        QMessageBox::warning(this,"seek",msg);
    }
    int count = 0;
    x_.clear();y_.clear();
    for (; it->Valid() && count < 128; it->Prev()) {
        //遇到了instrument数据=
        if (it->value().size() != sizeof(DepthMarketDataField)) {
            if (count ==0) QMessageBox::warning(this,"seek",msg);
            break;
        }
        auto mdf = (DepthMarketDataField*)it->value().data();
        //遇到了前后两个结束item
        if (mdf->InstrumentID[0] == 0) {
            if (count ==0) QMessageBox::warning(this,"seek",msg);
            break;
        }
        count++;
        onGotTick(mdf);
        x_.append(count);
    }
    delete it;
    drawGraph();
}

void HistoryForm::on_delButton_clicked(){
    leveldb::DB* db = g_sm->dataPump()->getLevelDB();
    QString key = ui->lineEdit->text();
    leveldb::ReadOptions options;
    std::string val;
    leveldb::Status status = db->Get(options,key.toStdString(),&val);
    if (status.ok()){
        leveldb::WriteOptions options;
        status = db->Delete(options,key.toStdString());
    }

    if(status.ok()){
        QMessageBox::about(this,"del","ok");
    }
    else{
       QString errStr = QString::fromStdString(status.ToString());
       QMessageBox::warning(this,"del",errStr);
    }
}

void HistoryForm::onGotTick(void* tick)
{
    auto mdf = (DepthMarketDataField*)tick;
    y_.append(mdf->LastPrice);

    QVariantMap mdItem;
    mdItem.insert("InstrumentID", mdf->InstrumentID);
    mdItem.insert("TradingDay", mdf->TradingDay);
    mdItem.insert("UpdateTime", mdf->UpdateTime);
    mdItem.insert("UpdateMillisec", mdf->UpdateMillisec);
    mdItem.insert("LastPrice", mdf->LastPrice);
    mdItem.insert("Volume", mdf->Volume);
    mdItem.insert("OpenInterest", mdf->OpenInterest);
    mdItem.insert("BidPrice1", mdf->BidPrice1);
    mdItem.insert("BidVolume1", mdf->BidVolume1);
    mdItem.insert("AskPrice1", mdf->AskPrice1);
    mdItem.insert("AskVolume1", mdf->AskVolume1);

    //根据id找到对应的行，然后用列的text来在map里面取值设置到item里面=
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);
    for (int i = 0; i < instruments_col_.count(); i++) {
        QVariant raw_val = mdItem.value(instruments_col_.at(i));
        QString str_val = raw_val.toString();
        if (raw_val.type() == QMetaType::Double || raw_val.type() == QMetaType::Float) {
            str_val = QString().sprintf("%6.1f", raw_val.toDouble());
        }

        QTableWidgetItem* item = new QTableWidgetItem(str_val);
        ui->tableWidget->setItem(row, i, item);
    }
}

void HistoryForm::initGraph()
{
    ui->qcustomplot->addGraph();
}

void HistoryForm::drawGraph(){
    ui->qcustomplot->graph()->setData(x_, y_);
    ui->qcustomplot->graph()->rescaleAxes(false);
    ui->qcustomplot->xAxis->scaleRange(1.1, ui->qcustomplot->xAxis->range().center());
    ui->qcustomplot->yAxis->scaleRange(1.1, ui->qcustomplot->yAxis->range().center());
    ui->qcustomplot->xAxis->setTicks(true);
    ui->qcustomplot->yAxis->setTicks(true);
    ui->qcustomplot->axisRect()->setupFullAxesBox();
    ui->qcustomplot->replot();
}
