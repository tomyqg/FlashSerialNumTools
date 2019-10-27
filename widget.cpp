#include "widget.h"
#include "ui_widget.h"
#include <QIntValidator>

uint16_t crc16(uint8_t *buffer, uint16_t buffer_length)
{
    uint8_t crc_hi = 0xFF; /* high CRC byte initialized */
    uint8_t crc_lo = 0xFF; /* low CRC byte initialized */
    unsigned int i; /* will index into CRC lookup */

    /* pass through message buffer */
    while (buffer_length--) {
        i = crc_hi ^ *buffer++; /* calculate the CRC  */
        crc_hi = crc_lo ^ table_crc_hi[i];
        crc_lo = table_crc_lo[i];
    }

    return (uint16_t)(crc_hi << 8 | crc_lo);
}

void Delay_MSec_Suspend(unsigned int msec)
{
    QTime _Timer = QTime::currentTime().addMSecs(msec);
    while( QTime::currentTime() < _Timer );
}


Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , mytcpclient(new MyTCPClient)
    , connPDlg(new MyProgressDlg(this))
    , fileDlg(new QFileDialog(this))
    , tcpConnectTimer(new QTimer)
{
    ui->setupUi(this);

    setWindowTitle(tr("agv充电站序列号下载工具"));
    this->windowState();
    this->setFixedSize( WIDGET_WIDTH, WIDGET_HEIGTH_MAIN );

    ui->btn_showDetailInfo->setCheckable(true);
    ui->textBrowser_DetailInfo->setVisible(false);

    ui->lineEdit_ProductCode_Pre->setText(tr("BWCH-HK"));
    ui->lineEdit_ProductCode_Pre->setDisabled(true);
    ui->lineEdit_ProductCode->setText(tr("0001"));

    ui->lineEdit_ProductSerialNum_Pre->setText(tr("N07480A"));
    ui->lineEdit_ProductSerialNum_Pre->setDisabled(true);
    ui->lineEdit_ProductSerialNum->setText(tr("001"));

    QString time=QDateTime::currentDateTime().toString(QString("yyMMdd"));
    ui->lineEdit_ProductSerialNum_Date->setText(time);

    // 设置右键策略
    ui->textBrowser_DetailInfo->setContextMenuPolicy(Qt::CustomContextMenu);

    // 产品编码：限制输入为整数，范围为1-9999
//    QRegExp rx1("[1-9]\\d{0,3}");
//    QRegExpValidator *validator1 = new QRegExpValidator(rx1, this);
//    ui->lineEdit_ProductCode->setValidator( validator1 );

//    // 生产序号：限制输入为整数，范围为1-999
//    QRegExp rx2("[1-9]\\d{0,2}");
//    QRegExpValidator *validator2 = new QRegExpValidator(rx2, this);
//    ui->lineEdit_ProductSerialNum->setValidator( validator2 );

    loadSettings();
    connect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientButtonClicked()));
}

Widget::~Widget()
{
    onDeviceDisconn();
    saveSettings();
    delete ui;
    delete mytcpclient;
    delete connPDlg;
}

void Widget::loadSettings()
{
    settingsFileDir = QApplication::applicationDirPath() + "/config.ini";
    QSettings settings(settingsFileDir, QSettings::IniFormat);

    QFileInfo fi(settingsFileDir);
    QDateTime date;

    if(!fi.exists()) {
        qDebug("file config.ini do not exist, creat new, set new configuration");
        date = fi.created();
        qDebug() << "create date: " << date.currentDateTime();
        ui->lineEdit_TcpClientTargetIP->setText(settings.value("TCP_CLIENT_TARGET_IP", "10.10.100.254").toString());
        ui->lineEdit_TcpClientTargetPort->setText(settings.value("TCP_CLIENT_TARGET_PORT", 8899).toString());
        saveSettings();
    }else{
        qDebug("file config.ini already exist, load configration");
        ui->lineEdit_TcpClientTargetIP->setText(settings.value("TCP_CLIENT_TARGET_IP").toString());
        ui->lineEdit_TcpClientTargetPort->setText(settings.value("TCP_CLIENT_TARGET_PORT").toString());
    }
}

void Widget::saveSettings()
{
    QSettings settings(settingsFileDir, QSettings::IniFormat);
    settings.setValue("TCP_CLIENT_TARGET_IP", ui->lineEdit_TcpClientTargetIP->text());
    settings.setValue("TCP_CLIENT_TARGET_PORT", ui->lineEdit_TcpClientTargetPort->text());
    settings.sync();
}

bool Widget::setupConnection(quint8 type)
{
    bool isSuccess = false;

    switch (type)
    {
    case TCPCLIENT:
        isSuccess = true;
        tcpClientTargetAddr.setAddress(ui->lineEdit_TcpClientTargetIP->text());
        tcpClientTargetPort = ui->lineEdit_TcpClientTargetPort->text().toInt();
        if (mytcpclient == nullptr)
        {
            mytcpclient = new MyTCPClient;
        }
        mytcpclient->connectTo(tcpClientTargetAddr, tcpClientTargetPort);
        break;
    }
    return isSuccess;
}

void Widget::onTcpClientButtonClicked()
{
    disconnect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientButtonClicked()));

    if (setupConnection(TCPCLIENT))
    {
        ui->lineEdit_TcpClientTargetIP->setDisabled(true);
        ui->lineEdit_TcpClientTargetPort->setDisabled(true);

        connect(mytcpclient, SIGNAL(myClientConnected(QString, quint16)), this, SLOT(onTcpClientNewConnection(QString, quint16)));
        connect(mytcpclient, SIGNAL(connectionFailed()), this, SLOT(onTcpClientTimeOut()));
    }
    logMsg("打开网络接口");

    // 弹出连接中的对话框
    connPDlg->move(this->x() + this->width()/2 - connPDlg->width()/2, this->y() + this->height()/2 - connPDlg->height()/2);
    connPDlg->show();
    connPDlg->progressCnt = 0;

    // 启动连接超时定时器
    qDebug() << "start tcp connect timer";
    tcpConnectTimer->start(200);
    connect(tcpConnectTimer, SIGNAL(timeout()), this, SLOT(onTcpClientConnectFailed()) );
}

void Widget::onTcpClientConnectFailed()
{
    connPDlg->progressCnt ++;
    connPDlg->setValue( connPDlg->progressCnt );

    if( connPDlg->progressCnt >= 10)
    {
        connPDlg->progressCnt = 0;
        disconnect(tcpConnectTimer, SIGNAL(timeout()), this, SLOT(onTcpClientConnectFailed()) );

        onTcpClientStopButtonClicked();

        qDebug() << "in tcp connect failed, stop tcp connect timer";
        logMsg("网络接口打开失败");
        QMessageBox::warning(this, "网络连接失败", "请检查本机是否连接网络", u8"退出");
    }
}

void Widget::onTcpClientNewConnection(const QString &from, quint16 port)
{
    disconnect(mytcpclient, SIGNAL(myClientConnected(QString, quint16)), this, SLOT(onTcpClientNewConnection(QString, quint16)));
    disconnect(mytcpclient, SIGNAL(connectionFailed()), this, SLOT(onTcpClientTimeOut()));
//    connect(ui->button_TcpClient, SIGNAL(clicked()), this, SLOT(onTcpClientStopButtonClicked()));
    connect(mytcpclient, SIGNAL(myClientDisconnected()), this, SLOT(onTcpClientDisconnected()));

    ui->btn_Download->setDisabled(false);

    connect(mytcpclient, SIGNAL(newMessage(QString, QByteArray)), this, SLOT(onTcpClientAppendMessage(QString, QByteArray)));

    // 停止超时计时器
    tcpConnectTimer->stop();

    logMsg("网络接口打开成功");

    // 发送设备查询指令
    qDebug("new connection established, Send query dev instructions");

    quint32 timeout = 0;
    E_FlashStep  step = step_chk_conn;
    // 发送查询设备是否在线
    while( true )
    {
        switch( step ) {
        case step_chk_conn:
            // 发送conn指令
            flash_chk_dev_conn();
            break;
        case step_flash_serial_num:
            // 发送烧录序列号指令
            flash_serial_num();
            break;
        case step_flash_product_code:
            // 发送烧录时间指令
            flash_product_code();
            break;
        default:
            onTcpClientStopButtonClicked();
            QMessageBox::warning(this, "下载失败", "请检查设备是否正常!", u8"退出");
            break;
        }

        // waiting
        while( true )
        {
            if( m_result == dev_conn_success )
            {
                step = step_flash_serial_num;
                connPDlg->progressCnt = 50;
                connPDlg->setValue( connPDlg->progressCnt );
                break;
            }
            else if( m_result == flash_serial_num_success )
            {
                step = step_flash_product_code;
                connPDlg->progressCnt = 100;
                connPDlg->setValue( connPDlg->progressCnt );
                break;
            }
            else if( m_result == flash_serial_num_failed )
            {
                goto flash_failed;
            }
            else if( m_result == flash_product_code_success )
            {
                step = step_flash_serial_num;
                connPDlg->progressCnt = 150;
                connPDlg->setValue( connPDlg->progressCnt );
                onTcpClientStopButtonClicked();

                // 显示下载成功
                QMessageBox::information(this, "提示", "下载成功", u8"确定");
                return;
            }
            else if( m_result == flash_product_code_failed )
            {
                goto flash_failed;
            }
            else if( m_result == none )
            {
                Delay_MSec_Suspend(100);
                timeout ++;
                qDebug() << "timeout: " << timeout;
                connPDlg->setValue( ++connPDlg->progressCnt );
                if( timeout > 50 ) // 5s超时
                {
                    goto flash_failed;
                }
            }
        }

        timeout = 0;
    }
    return;

flash_failed:
    onTcpClientStopButtonClicked();
    // 显示下载失败
    qDebug() << "step: " << step;
    if( m_result == none )
    {
        logMsg("设备没有响应，请检查通信连接");
        QMessageBox::warning(this, "下载失败", "无法与设备建立连接!", u8"退出");
    }
    else
        QMessageBox::warning(this, "下载失败", "请检查设备是否正常!", u8"退出");

    return;
}

void Widget::onTcpClientTimeOut()
{
    disconnect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientStopButtonClicked()));
    disconnect(mytcpclient, SIGNAL(myClientConnected(QString, quint16)), this, SLOT(onTcpClientNewConnection(QString, quint16)));
    disconnect(mytcpclient, SIGNAL(connectionFailed()), this, SLOT(onTcpClientTimeOut()));

    ui->lineEdit_TcpClientTargetIP->setDisabled(false);
    ui->lineEdit_TcpClientTargetPort->setDisabled(false);

    mytcpclient->closeClient();
    connect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientButtonClicked()));

    logMsg("打开网络接口超时，网络接口打开失败");

    // 停止超时计时器
    tcpConnectTimer->stop();

    QMessageBox::warning(this, "设备连接失败", "请检查设备地址是否正确!", u8"退出");
}

void Widget::onTcpClientStopButtonClicked()
{
    qDebug("stop tcp client button clicked");

    // 断开设备按钮按下后，需要执行以下两个操作，注意：顺序不能反了!!!
    // 1. 断开和设备的连接
    onDeviceDisconn();

    // 2. 断开tcp连接
    mytcpclient->abort();
    onTcpClientDisconnected();
}

void Widget::onTcpClientDisconnectButtonClicked()
{
    mytcpclient->disconnectCurrentConnection();
}

void Widget::onTcpClientDisconnected()
{
    disconnect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientDisconnectButtonClicked()));
    disconnect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientStopButtonClicked()));
    disconnect(mytcpclient, SIGNAL(myClientConnected(QString, quint16)), this, SLOT(onTcpClientNewConnection(QString, quint16)));
    disconnect(mytcpclient, SIGNAL(connectionFailed()), this, SLOT(onTcpClientTimeOut()));
    disconnect(mytcpclient, SIGNAL(myClientDisconnected()), this, SLOT(onTcpClientDisconnected()));
    disconnect(mytcpclient, SIGNAL(newMessage(QString, QByteArray)), this, SLOT(onTcpClientAppendMessage(QString, QByteArray)));

    connect(ui->btn_Download, SIGNAL(clicked()), this, SLOT(onTcpClientButtonClicked()));

    mytcpclient->closeClient();
    mytcpclient->close();

//    ui->btn_Download->setText("连接设备");
    ui->btn_Download->setDisabled(false);
    ui->lineEdit_TcpClientTargetIP->setDisabled(false);
    ui->lineEdit_TcpClientTargetPort->setDisabled(false);

    connPDlg->reset();
    connPDlg->hide();
//    connPDlg->progressTimer->stop();
    connPDlg->progressCnt = 0;
}

void Widget::onTcpClientAppendMessage(const QString &from, const QByteArray &message)
{
    if (from.isEmpty() || message.isEmpty())
    {
        return;
    }

    // 处理下位机传过来的信息
    quint32 frame_len = 0;   // 报文长度，从报文长度高字节之后，CRC校验低字节之前的部分
    qDebug("\r\n\r\n<<------------解析充电站信息---------->>");
    // 把所有数据读取出来
    QByteArray::const_iterator it = message.begin();
    for( quint16 idx=0 ; it != message.end(); it++, idx++ )
    {
        rec_buff[idx] = (quint8)(*it);
    }
    frame_len = rec_buff[4] & 0xff;
    frame_len |= ( ( (rec_buff[5] & 0xff) << 8) & 0xFF00 );
    qDebug("whole frame size:%d, frame_len: %d, (hex:0x%04X)", message.size(), frame_len, frame_len);

    // 校验帧头
    quint8 buff_header_std[] = {0x55, 0xAA, 0xA5, 0x5A};
    for( quint16 idx = 0; idx < 4; idx ++ )
    {
        qDebug("chk header: %02x", rec_buff[idx] );
        if( buff_header_std[idx] != rec_buff[idx] )
        {
            qDebug() << "header error";
            return;
        }
    }

    // 校验crc值
    quint16 crc_read = 0;
    quint16 crc_calc = 0;

    crc_calc = crc16( &rec_buff[4], frame_len+2  );
    crc_read = (rec_buff[frame_len+2+4] & 0xFF) | \
               ( ( ( rec_buff[frame_len+2+4+1] & 0xFF ) << 8) & 0xFF00 );
    if( crc_calc != crc_read )
    {
        qDebug("crc check failed, crc_read:%04X, crc_calc:%04X", crc_read, crc_calc);
        return;
    }
    else
    {
        qDebug("crc check success, crc_read:%04X, crc_calc:%04X", crc_read, crc_calc);
    }

    uint8_t fc = 0;
    uint8_t cc = 0;
    uint8_t result = 0;
    // 判断功能码和命令字
    fc = rec_buff[OFFSET_FC];
    cc = rec_buff[OFFSET_CC];
    result = rec_buff[OFFSET_RESULT];
    if( fc == (uint8_t)FC_QUERY ) {
        if(cc == CC_QUERY_CONN ) {
            qDebug("rec device ack(device in app state), device conn success");
            logMsg("设备响应成功，设备在线\r\n");
            m_result = dev_conn_success;
        }
    }else if( fc == (uint8_t)FC_CTL ) {
        if(cc == CC_FLASH_SERIAL_NUM ) {
            if( result != 0 ) {
                qDebug("rec device ack, flash serial num success");
                logMsg("设备响应成功，生产序号下载成功\r\n");
                m_result = flash_serial_num_success;
            }else{
                logMsg("设备响应成功，但是生产序号下载失败！！！\r\n");
                m_result = flash_serial_num_failed;
            }
        }else if(cc == CC_FLASH_CODE ) {
            if( result != 0 ) {
                qDebug("rec device ack, flash date success");
                logMsg("设备响应成功，产品编码下载成功\r\n");
                m_result = flash_product_code_success;
            }else{
                logMsg("设备响应成功，产品产品编码下载失败！！！\r\n");
                m_result = flash_product_code_failed;
            }
        }
    }
    qDebug() << "rec_len:" << message.length();
    qDebug() << "data: " << message;
    qDebug("%02x", message.at(0));
}

void Widget::onDeviceDisconn()
{
    if( devOnlineStatus != false )
    {
        devOnlineStatus = false;
    }

    tcpConnectTimer->stop();
}

void Widget::onDeviceConnSuccess()
{
    if( devOnlineStatus != true )
    {
        devOnlineStatus = true;
        m_result = dev_conn_success;

        // 隐藏连接中的对话框
//        connPDlg->progressCnt = 100;
//        connPDlg->setValue( connPDlg->progressCnt );
//        connPDlg->progressCnt = 0;
//        connPDlg->setValue( connPDlg->progressCnt );
//        connPDlg->reset();
//        connPDlg->hide();
//        connPDlg->progressTimer->stop();

        // 设置可以选择文件
//        ui->transmitBrowse->setEnabled(true);
//        if(ui->transmitPath->text().isEmpty() != true) {
//            qDebug("set enable");
//            ui->transmitButton->setEnabled(true);
//        }

//        QMessageBox::information(this, "提示", "设备连接成功", u8"确定");
    }
    else
    {
        // 启动操作超时
//        connTimer->start(OP_TIMEOUT);
    }

}

void Widget::on_btn_showDetailInfo_toggled(bool checked)
{
    qDebug("detail btn");
    if(checked)
    {
        this->setFixedSize( WIDGET_WIDTH, WIDGET_HEIGTH_EXT);
        ui->textBrowser_DetailInfo->setVisible(checked);
        ui->btn_showDetailInfo->setText(tr("隐藏详细信息"));
    }
    else
    {
        this->setFixedSize( WIDGET_WIDTH, WIDGET_HEIGTH_MAIN);
        ui->textBrowser_DetailInfo->setVisible(false);
        ui->btn_showDetailInfo->setText(tr("显示详细信息"));
    }
}

void Widget::flash_chk_dev_conn()
{
    uint8_t query_buff[] = { 0x55,0xAA,0xA5,0x5A,0x07,0x00,0x00,0x00,0x04,0xFF,0xFF,0xFF,0xFF,0x94,0xDC };
    quint32 len = 0;
    QByteArray data;

    qDebug("send conn package");
    len = sizeof(query_buff)/sizeof(uint8_t);
    qDebug() << "send len:" << len;
    for(quint32 i=0; i<len; i++)
        data.append( query_buff[i] );
    mytcpclient->sendMessage(data);

    logMsg("查询设备是否在线，发送查询指令...");
    logMsg("等待设备响应...");

    m_result = none;
}

void Widget::flash_serial_num()
{
    qDebug() << "flash product serail num";
    uint8_t bTcpDataBuf[1024];
    uint32_t Len = 0;

    bTcpDataBuf[Len++] = 0x55;
    bTcpDataBuf[Len++] = 0xAA; //传输标志，一般默认为0x00 0x00
    bTcpDataBuf[Len++] = 0xA5;
    bTcpDataBuf[Len++] = 0x5A; //协议标志，0代表Modbus，1代表UNI-TE

    bTcpDataBuf[Len++] = 0x00; //报文长度，低字节，下面更新
    bTcpDataBuf[Len++] = 0x00; //报文长度，高字节，下面更新

    bTcpDataBuf[Len++] = 0x00; // 预留，固定为0x00

    bTcpDataBuf[Len++] = 0x04; // 命令字
    bTcpDataBuf[Len++] = 0x03; //功能码：0x03->Write 0x04->Read

    bTcpDataBuf[Len++] = 0xFF; // 充电站地址低字节
    bTcpDataBuf[Len++] = 0xFF; // 充电站地址高字节

    bTcpDataBuf[Len++] = 0xFF; // 输出通道地址低字节
    bTcpDataBuf[Len++] = 0xFF; // 输出通道地址高字节

    qDebug() << "input data";
    QString str = ui->lineEdit_ProductSerialNum_Pre->text();
            str.append(ui->lineEdit_ProductSerialNum_Date->text());
            str.append(ui->lineEdit_ProductSerialNum->text());

    logMsg("开始下载生产序号，生产序号为：");
    logMsg(str);

    QByteArray ba = str.toLatin1();
    int data_len = ba.length();
    for (int i=0; i<data_len; i++) {
        bTcpDataBuf[Len++] = (uint8_t)ba.at(i);
    }

    // 更新报文长度
    bTcpDataBuf[4] = (7 + data_len) & 0xFF;
    bTcpDataBuf[5] = ( (7 + data_len) >> 8 ) & 0xFF;

    // 更新CRC值
    uint32_t crc_calc = 0;
    crc_calc = crc16( bTcpDataBuf + 4, (WIFI_SIZE_FRAME_LEN + 7 + data_len) );
    bTcpDataBuf[Len++] = crc_calc & 0xFF;
    bTcpDataBuf[Len++] = ( crc_calc >> 8 ) & 0xFF;

    // 发送数据
    QByteArray data;
    for (quint32 i=0; i<Len; i++) {
        data.append(bTcpDataBuf[i]);
    }
    mytcpclient->sendMessage(data);

    logMsg("等待设备响应...");

    m_result = none;
}

void Widget::flash_product_code()
{
    qDebug() << "flash product code";
    uint8_t bTcpDataBuf[1024];
    uint32_t Len = 0;

    bTcpDataBuf[Len++] = 0x55;
    bTcpDataBuf[Len++] = 0xAA; //传输标志，一般默认为0x00 0x00
    bTcpDataBuf[Len++] = 0xA5;
    bTcpDataBuf[Len++] = 0x5A; //协议标志，0代表Modbus，1代表UNI-TE

    bTcpDataBuf[Len++] = 0x00; //报文长度，低字节，下面更新
    bTcpDataBuf[Len++] = 0x00; //报文长度，高字节，下面更新

    bTcpDataBuf[Len++] = 0x00; // 预留，固定为0x00

    bTcpDataBuf[Len++] = 0x05; // 命令字
    bTcpDataBuf[Len++] = 0x03; //功能码：0x03->Write 0x04->Read

    bTcpDataBuf[Len++] = 0xFF; // 充电站地址低字节
    bTcpDataBuf[Len++] = 0xFF; // 充电站地址高字节

    bTcpDataBuf[Len++] = 0xFF; // 输出通道地址低字节
    bTcpDataBuf[Len++] = 0xFF; // 输出通道地址高字节

//    QString time=QDateTime::currentDateTime().toString(QString("yyyy-MM-dd HH:mm:ss"));

    QString str = ui->lineEdit_ProductCode_Pre->text();
            str.append(ui->lineEdit_ProductCode->text());

    QByteArray ba = str.toLatin1();
    int data_len = ba.length();
    for (int i=0; i<data_len; i++) {
        bTcpDataBuf[Len++] = (uint8_t)ba.at(i);
    }

    logMsg("开始下载产品编码，编码为：");
    logMsg(str);

    // 更新报文长度
    bTcpDataBuf[4] = (7 + data_len) & 0xFF;
    bTcpDataBuf[5] = ( (7 + data_len) >> 8 ) & 0xFF;

    // 更新CRC值
    uint32_t crc_calc = 0;
    crc_calc = crc16( bTcpDataBuf + 4, (WIFI_SIZE_FRAME_LEN + 7+ data_len) );
    bTcpDataBuf[Len++] = crc_calc & 0xFF;
    bTcpDataBuf[Len++] = ( crc_calc >> 8 ) & 0xFF;

    // 发送数据
    QByteArray data;
    for (quint32 i=0; i<Len; i++) {
        data.append(bTcpDataBuf[i]);
    }
    mytcpclient->sendMessage(data);

    logMsg("等待设备响应...");

    m_result = none;
}

void Widget::logMsg(QString msg)
{
    QString str = QDateTime::currentDateTime().toString(QString("[yyyy-MM-dd HH:mm:ss] "));
    str.append(msg);
    ui->textBrowser_DetailInfo->append(str);
}

void Widget::on_textBrowser_DetailInfo_customContextMenuRequested(const QPoint &pos)
{
    static QMenu *menu = nullptr;
    if( menu == nullptr ) {
        menu = new QMenu( this );
        QAction *action1 = new QAction("清空", this);
        connect(action1, SIGNAL(triggered()), this, SLOT(onClearDetailInfo()));

        menu->addAction(action1);
    }

    menu->exec(QCursor::pos());
}

void Widget::onClearDetailInfo()
{
    ui->textBrowser_DetailInfo->clear();
}

//MyProgressDlg::MyProgressDlg(QWidget *parent):
//    progressTimer(new QTimer())
MyProgressDlg::MyProgressDlg(QWidget *parent)
{
    setWindowTitle("下载序列号");
    setMinimum(0);
    setMaximum( WAITING_TIMEOUT/SUSPEND_TIME * 3 );
    setValue(0);
    setLabelText("下载中...");
    setWindowFlag( Qt::FramelessWindowHint );
    setModal(true);
    QPushButton *btn = nullptr;
    setCancelButton(btn);
    reset(); // 必须添加，否则new完后，会自动弹出
    hide();
}

MyProgressDlg::~MyProgressDlg()
{
}

void MyProgressDlg::keyPressEvent(QKeyEvent *event)
{
    qDebug("widget, esc pressed");
    switch (event->key())
    {
    case Qt::Key_Escape:
        qDebug("widget, esc pressed");
        break;
    default:
        QDialog::keyPressEvent(event);
        break;
    }
}

