#include "hexview.h"
#include "ui_hexview.h"

HexView::HexView(QHexEditData* hexeditdata, const QString& viewname, QLabel *labelinfo, QWidget *parent): AbstractView(viewname, labelinfo, parent), ui(new Ui::HexView), _disassemblerdialog(nullptr), _formattree(nullptr), _hexeditdata(hexeditdata), _toolbar(nullptr), _signscanenabled(false), _entropyenabled(false)
{
    ui->setupUi(this);
    ui->hSplitter->setStretchFactor(0, 1);
    ui->vSplitter->setStretchFactor(0, 1);
    ui->dataView->setData(hexeditdata);
    ui->hexEdit->setData(hexeditdata);

    this->_binaryviewdialog = new BinaryViewDialog(hexeditdata, this);
    this->_binaryviewdialog->setWindowTitle(QString("'%1' Binary View").arg(viewname));

    this->_signaturecolor = QColor(0xFF, 0x8C, 0x8C);
    this->_formatmodel = new FormatModel(hexeditdata, this);
    ui->tvFormat->setModel(this->_formatmodel);

    this->createToolBar();
    this->inspectData(hexeditdata);

    connect(ui->hexEdit, SIGNAL(positionChanged(qint64)), ui->dataView->model(), SLOT(setOffset(qint64)));
    connect(ui->hexEdit, SIGNAL(positionChanged(qint64)), this, SLOT(updateOffset(qint64)));
    connect(ui->hexEdit, SIGNAL(selectionChanged(qint64)), this, SLOT(updateSelLength(qint64)));
    connect(ui->hexEdit, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onHexEditCustomContextMenuRequested(QPoint)));
    connect(ui->hexEdit, SIGNAL(verticalScrollBarValueChanged(int)), ui->binaryNavigator, SLOT(renderMap(int)));

    connect(ui->tvFormat, SIGNAL(setBackColor(FormatElement*)), this, SLOT(onSetBackColor(FormatElement*)));
    connect(ui->tvFormat, SIGNAL(removeBackColor(FormatElement*)), this, SLOT(onRemoveBackColor(FormatElement*)));
    connect(ui->tvFormat, SIGNAL(formatObjectSelected(FormatElement*)), this, SLOT(onFormatObjectSelected(FormatElement*)));
    connect(ui->tvFormat, SIGNAL(exportAction(FormatElement*)), this, SLOT(exportData(FormatElement*)));
    connect(ui->tvFormat, SIGNAL(importAction(FormatElement*)), this, SLOT(importData(FormatElement*)));
    connect(ui->tvFormat, SIGNAL(gotoOffset(qint64)), ui->hexEdit, SLOT(setCursorPos(qint64)));

    connect(this->_binaryviewdialog, SIGNAL(gotoTriggered(qint64)), ui->hexEdit, SLOT(selectPos(qint64)));
}

bool HexView::loadFormat(FormatList::Format &format, int64_t baseoffset)
{
    this->_formattree = SDKManager::parseFormat(format.id(), baseoffset, this->_hexeditdata);
    this->_formatmodel->setFormatTree(this->_formattree);

    if(format.optionsCount() > 0)
    {
        this->_tbformat->setPopupMode(QToolButton::MenuButtonPopup);
        this->_tbformat->setMenu(new OptionMenu(SDKManager::state(), ui->hexEdit, format));
    }
    else
    {
        this->_tbformat->setPopupMode(QToolButton::DelayedPopup);
        this->_tbformat->setMenu(nullptr);
    }

    for(int i = 0; i < this->_formatmodel->columnCount(); i++)
        ui->tvFormat->resizeColumnToContents(i);

    return !this->_formattree->isEmpty();
}

void HexView::save()
{
    this->_hexeditdata->save();
}

void HexView::save(QString filename)
{
    QFile f(filename);
    this->_hexeditdata->saveTo(&f);
    f.close();
}

HexView::~HexView()
{
    FormatList::removeLoadedFormat(this->_hexeditdata);
    delete ui;
}

bool HexView::canSave() const
{
    return true;
}

void HexView::updateStatusBar()
{
    QString offset = QString("%1").arg(ui->hexEdit->cursorPos(), ui->hexEdit->addressWidth(), 16, QLatin1Char('0')).toUpper();
    QString size = QString("%1").arg(ui->hexEdit->selectionLength(), ui->hexEdit->addressWidth(), 16, QLatin1Char('0')).toUpper();
    this->updateInfoText(QString("<b>Offset:</b> %1h&nbsp;&nbsp;&nbsp;&nbsp;<b>Size:</b> %2h").arg(offset, size));
}

void HexView::createToolBar()
{
    this->_toolbar = new ActionToolBar(ui->hexEdit, ui->tbContainer);

    this->_tbformat = new QToolButton();
    this->_tbformat->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    this->_tbformat->setIcon(QIcon(":/misc_icons/res/format.png"));
    this->_tbformat->setText("Formats");
    this->_toolbar->addWidget(this->_tbformat);

    this->_actbyteview = this->_toolbar->addAction(QIcon(":/action_icons/res/entropy.png"), "Map View");
    this->_actbyteview->setCheckable(true);
    this->_actbinaryview = this->_toolbar->addAction(QIcon(":/action_icons/res/binview.png"), "Binary View");
    this->_actdisassembler = this->_toolbar->addAction(QIcon(":/action_icons/res/cpu.png"), "Disassembler");
    this->_actdisassembler->setVisible(false);

    this->_toolbar->addSeparator();
    this->_toolbar->createActions(ui->actionWidget, ActionToolBar::AllActions);

    connect(this->_tbformat, SIGNAL(clicked()), this, SLOT(onLoadFormatClicked()));
    connect(this->_actbyteview, SIGNAL(triggered()), this, SLOT(onMapViewTriggered()));
    connect(this->_actbinaryview, SIGNAL(triggered()), this, SLOT(onBinaryViewTriggered()));
    connect(this->_actdisassembler, SIGNAL(triggered()), this, SLOT(onDisassemblerTriggered()));

    QVBoxLayout* vl = new QVBoxLayout();
    vl->setContentsMargins(0, 0, 0, 0);
    vl->addWidget(this->_toolbar);

    ui->tbContainer->setLayout(vl);
}

void HexView::inspectData(QHexEditData *hexeditdata)
{
    ui->binaryNavigator->setData(ui->hexEdit);
    ui->chartWidget->plot(hexeditdata);
    ui->stringsWidget->scan(hexeditdata);
    ui->signaturesWidget->scan(hexeditdata);

    connect(ui->stringsWidget, SIGNAL(gotoTriggered(qint64,qint64)), ui->hexEdit, SLOT(setSelectionRange(qint64,qint64)));
    connect(ui->signaturesWidget, SIGNAL(gotoTriggered(qint64,qint64)), ui->hexEdit, SLOT(setSelectionRange(qint64,qint64)));
}

void HexView::updateOffset(qint64)
{
    this->updateStatusBar();
}

void HexView::updateSelLength(qint64 size)
{
    this->updateStatusBar();

    if(!this->_toolbar)
        return;

    if(!size)
    {
        this->_toolbar->setEditActionsEnabled(false);
        ui->actionWidget->byteOpsAction()->hide();
    }
    else
        this->_toolbar->setEditActionsEnabled(true);
}

void HexView::onLoadFormatClicked()
{
    FormatsDialog fd(this->_hexeditdata->length(), this->topLevelWidget());
    int res = fd.exec();

    if(res == FormatsDialog::Accepted)
    {
        this->_formatid = fd.selectedFormat();
        FormatList::Format& format = FormatList::formatFromId(this->_formatid);

        if(this->loadFormat(format, fd.offset()))
        {
            FormatList::addLoadedFormat(this->_formatid, this->_formattree, this->_hexeditdata);
            ui->tabWidget->setCurrentIndex(2); /* Select Format Page */

            if(format.canDisassemble())
            {
                this->_disassemblerdialog = new DisassemblerDialog(this->_hexeditdata, this->_formattree, this);
                this->_disassemblerdialog->setWindowTitle(QString("'%1' Disassembly").arg(this->viewName()));
                this->_actdisassembler->setVisible(true);
            }
            else
            {
                this->_actdisassembler->setVisible(false);
                this->_disassemblerdialog = nullptr;
            }
        }
    }
}

void HexView::onMapViewTriggered()
{
    if(this->_actbyteview->isChecked())
        ui->binaryNavigator->displayEntropy();
    else
        ui->binaryNavigator->displayDefault();
}

void HexView::onBinaryViewTriggered()
{
    if(!this->_binaryviewdialog)
        return;

    if(this->_binaryviewdialog->isVisible())
        this->_binaryviewdialog->raise();
    else
        this->_binaryviewdialog->show();
}

void HexView::onDisassemblerTriggered()
{
    if(!this->_disassemblerdialog)
        return;

    if(this->_disassemblerdialog->isVisible())
        this->_disassemblerdialog->raise();
    else
        this->_disassemblerdialog->show();
}

void HexView::onHexEditCustomContextMenuRequested(const QPoint &pos)
{
    QPoint newpos = ui->hexEdit->mapToGlobal(pos);
    this->_toolbar->actionMenu()->popup(newpos);
}

void HexView::onSetBackColor(FormatElement *formatelement)
{
    QColor c = QColorDialog::getColor(Qt::white, this);

    if(c.isValid())
    {
        uint64_t offset = formatelement->offset();
        ui->hexEdit->highlightBackground(offset, (offset + formatelement->size() - 1), c);
    }
}

void HexView::onRemoveBackColor(FormatElement *formatelement)
{
    uint64_t offset = formatelement->offset();
    ui->hexEdit->clearHighlight(offset, (offset + formatelement->size() - 1));
}

void HexView::onFormatObjectSelected(FormatElement *formatelement)
{
    uint64_t offset = formatelement->offset();
    ui->hexEdit->setSelection(offset, offset + formatelement->size());
}

void HexView::exportData(FormatElement *formatelement)
{
    ExportDialog ed(ui->hexEdit, this);
    ed.setFixedRange(formatelement->offset(), formatelement->endOffset());
    int res = ed.exec();

    if(res == ExportDialog::Accepted)
        ExporterList::exportData(ed.selectedExporter().id(), ed.fileName(), this->_hexeditdata, ed.startOffset(), ed.endOffset());
}

void HexView::importData(FormatElement *formatelement)
{
    QString s = QFileDialog::getOpenFileName(this, "Import binary file...");

    if(!s.isEmpty())
    {
        QFile f(s);
        f.open(QIODevice::ReadOnly);

        uint64_t offset = formatelement->offset();
        uint64_t size = qMin(static_cast<uint64_t>(f.size()), (formatelement->endOffset() - offset));

        if (size > 0)
        {
            QByteArray ba = f.read(size);
            QHexEditDataWriter writer(this->_hexeditdata);
            writer.replace(offset, size, ba);
        }
    }
}
