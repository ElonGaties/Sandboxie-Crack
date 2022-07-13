
void CSandMan::OnFileToRecover(const QString& BoxName, const QString& FilePath, const QString& BoxPath, quint32 ProcessId)
{
	CSandBoxPtr pBox = theAPI->GetBoxByName(BoxName);
	if ((!pBox.isNull() && pBox.objectCast<CSandBoxPlus>()->IsRecoverySuspended()) || IsDisableRecovery())
		return;

	if (theConf->GetBool("Options/InstantRecovery", true))
	{
		CRecoveryWindow* pWnd = ShowRecovery(pBox, false);

		//if (!theConf->GetBool("Options/AlwaysOnTop", false)) {
		//	SetWindowPos((HWND)pWnd->winId(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		//	QTimer::singleShot(100, this, [pWnd]() {
		//		SetWindowPos((HWND)pWnd->winId(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		//		});
		//}

		pWnd->AddFile(FilePath, BoxPath);
	}
	else
		m_pPopUpWindow->AddFileToRecover(FilePath, BoxPath, pBox, ProcessId);
}

bool CSandMan::OpenRecovery(const CSandBoxPtr& pBox, bool& DeleteShapshots, bool bCloseEmpty)
{
	auto pBoxEx = pBox.objectCast<CSandBoxPlus>();
	if (!pBoxEx) return false;
	if (pBoxEx->m_pRecoveryWnd != NULL) {
		pBoxEx->m_pRecoveryWnd->close();
		// todo: resuse window?
	}

	CRecoveryWindow* pRecoveryWindow = new CRecoveryWindow(pBox, false, this);
	if (pRecoveryWindow->FindFiles() == 0 && bCloseEmpty) {
		delete pRecoveryWindow;
		return true;
	}
	else if (pRecoveryWindow->exec() != 1)
		return false;
	DeleteShapshots = pRecoveryWindow->IsDeleteShapshots();
	return true;
}

CRecoveryWindow* CSandMan::ShowRecovery(const CSandBoxPtr& pBox, bool bFind)
{
	auto pBoxEx = pBox.objectCast<CSandBoxPlus>();
	if (!pBoxEx) return false;
	if (pBoxEx->m_pRecoveryWnd == NULL) {
		pBoxEx->m_pRecoveryWnd = new CRecoveryWindow(pBox, bFind == false);
		connect(pBoxEx->m_pRecoveryWnd, &CRecoveryWindow::Closed, [pBoxEx]() {
			pBoxEx->m_pRecoveryWnd = NULL;
		});
		pBoxEx->m_pRecoveryWnd->show();
	}
	/*else {
		pBoxEx->m_pRecoveryWnd->setWindowState((pBoxEx->m_pRecoveryWnd->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
		//SetForegroundWindow((HWND)pBoxEx->m_pRecoveryWnd->winId());
	}*/
	if(bFind)
		pBoxEx->m_pRecoveryWnd->FindFiles();
	return pBoxEx->m_pRecoveryWnd;
}

SB_PROGRESS CSandMan::RecoverFiles(const QString& BoxName, const QList<QPair<QString, QString>>& FileList, int Action)
{
	CSbieProgressPtr pProgress = CSbieProgressPtr(new CSbieProgress());
	QtConcurrent::run(CSandMan::RecoverFilesAsync, pProgress, BoxName, FileList, Action);
	return SB_PROGRESS(OP_ASYNC, pProgress);
}

void CSandMan::RecoverFilesAsync(const CSbieProgressPtr& pProgress, const QString& BoxName, const QList<QPair<QString, QString>>& FileList, int Action)
{
	SB_STATUS Status = SB_OK;

	int OverwriteOnExist = -1;

	QStringList Unrecovered;
	for (QList<QPair<QString, QString>>::const_iterator I = FileList.begin(); I != FileList.end(); ++I)
	{
		QString BoxPath = I->first;
		QString RecoveryPath = I->second;
		QString FileName = BoxPath.mid(BoxPath.lastIndexOf("\\") + 1);
		QString RecoveryFolder = RecoveryPath.left(RecoveryPath.lastIndexOf("\\") + 1);

		pProgress->ShowMessage(tr("Recovering file %1 to %2").arg(FileName).arg(RecoveryFolder));

		QDir().mkpath(RecoveryFolder);
		if (QFile::exists(RecoveryPath)) 
		{
			int Overwrite = OverwriteOnExist;
			if (Overwrite == -1)
			{
				bool forAll = false;
				int retVal = 0;
				QMetaObject::invokeMethod(theGUI, "ShowQuestion", Qt::BlockingQueuedConnection, // show this question using the GUI thread
					Q_RETURN_ARG(int, retVal),
					Q_ARG(QString, tr("The file %1 already exists, do you want to overwrite it?").arg(RecoveryPath)),
					Q_ARG(QString, tr("Do this for all files!")),
					Q_ARG(bool*, &forAll),
					Q_ARG(int, QDialogButtonBox::Yes | QDialogButtonBox::No),
					Q_ARG(int, QDialogButtonBox::No)
				);

				Overwrite = retVal == QDialogButtonBox::Yes ? 1 : 0;
				if (forAll)
					OverwriteOnExist = Overwrite;
			}
			if (Overwrite == 1)
				QFile::remove(RecoveryPath);
		}

		if (!QFile::rename(BoxPath, RecoveryPath))
			Unrecovered.append(BoxPath);
		else {
			QMetaObject::invokeMethod(theGUI, "OnFileRecovered", Qt::BlockingQueuedConnection, // show this question using the GUI thread
				Q_ARG(QString, BoxName),
				Q_ARG(QString, RecoveryPath),
				Q_ARG(QString, BoxPath)
			);
		}
	}

	if (!Unrecovered.isEmpty())
		Status = SB_ERR(SB_Message, QVariantList () << (tr("Failed to recover some files: \n") + Unrecovered.join("\n")));
	else if(FileList.count() == 1 && Action != 0)
	{
		std::wstring path = FileList.first().second.toStdWString();
		switch (Action)
		{
		case 1: // open
			ShellExecute(NULL, NULL, path.c_str(), NULL, NULL, SW_SHOWNORMAL);
			break;
		case 2: // explore
			ShellExecute(NULL, NULL, L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), NULL, SW_SHOWNORMAL);
			break;
		}
	}


	pProgress->Finish(Status);
}

void CSandMan::AddFileRecovered(const QString& BoxName, const QString& FilePath)
{
	CPanelWidgetEx* pRecoveryLog = m_pRecoveryLog;
	if (pRecoveryLog == NULL) {
		pRecoveryLog = m_pRecoveryLogWnd->m_pRecoveryLog;
		if (!pRecoveryLog) return;
	}

	QTreeWidgetItem* pItem = new QTreeWidgetItem(); // Time|Box|FilePath
	pItem->setText(0, QDateTime::currentDateTime().toString("hh:mm:ss.zzz"));
	pItem->setText(1, BoxName);
	pItem->setText(2, FilePath);
	pRecoveryLog->GetTree()->addTopLevelItem(pItem);

	pRecoveryLog->GetView()->verticalScrollBar()->setValue(pRecoveryLog->GetView()->verticalScrollBar()->maximum());
}

void CSandMan::OnFileRecovered(const QString& BoxName, const QString& FilePath, const QString& BoxPath)
{
	AddFileRecovered(BoxName, FilePath);

	CSandBoxPtr pBox = theAPI->GetBoxByName(BoxName);
	if (pBox)
		pBox.objectCast<CSandBoxPlus>()->UpdateSize();
}


////////////////////////////////////////////////////////////////////////////////////////
// CRecoveryLogWnd

CRecoveryLogWnd::CRecoveryLogWnd(QWidget *parent)
	: QDialog(parent)
{
	Qt::WindowFlags flags = windowFlags();
	flags |= Qt::CustomizeWindowHint;
	//flags &= ~Qt::WindowContextHelpButtonHint;
	//flags &= ~Qt::WindowSystemMenuHint;
	//flags &= ~Qt::WindowMinMaxButtonsHint;
	//flags |= Qt::WindowMinimizeButtonHint;
	//flags &= ~Qt::WindowCloseButtonHint;
	flags &= ~Qt::WindowContextHelpButtonHint;
	//flags &= ~Qt::WindowSystemMenuHint;
	setWindowFlags(flags);

	this->setWindowTitle(tr("Sandboxie-Plus - Recovery Log"));

	QGridLayout* pLayout = new QGridLayout();
	//pLayout->setMargin(3);
	
	m_pRecoveryLog = new CPanelWidgetEx();

	//m_pRecoveryLog->GetView()->setItemDelegate(theGUI->GetItemDelegate());
	((QTreeWidgetEx*)m_pRecoveryLog->GetView())->setHeaderLabels(tr("Time|Box Name|File Path").split("|"));

	QAction* pAction = new QAction(tr("Cleanup Recovery Log"));
	connect(pAction, SIGNAL(triggered()), m_pRecoveryLog->GetTree(), SLOT(clear()));
	m_pRecoveryLog->GetMenu()->insertAction(m_pRecoveryLog->GetMenu()->actions()[0], pAction);
	m_pRecoveryLog->GetMenu()->insertSeparator(m_pRecoveryLog->GetMenu()->actions()[0]);

	m_pRecoveryLog->GetView()->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_pRecoveryLog->GetView()->setSortingEnabled(false);

	connect(m_pRecoveryLog->GetTree(), SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(OnDblClick(QTreeWidgetItem*)));

	pLayout->addWidget(new QLabel(tr("the following files were recently recoered and moved oout of a sandbox.")), 0, 0);
	pLayout->addWidget(m_pRecoveryLog, 1, 0);
	this->setLayout(pLayout);

	restoreGeometry(theConf->GetBlob("RecoveryLogWindow/Window_Geometry"));
}

CRecoveryLogWnd::~CRecoveryLogWnd()
{
	theConf->SetBlob("RecoveryLogWindow/Window_Geometry", saveGeometry());
}

void CRecoveryLogWnd::closeEvent(QCloseEvent *e)
{
	emit Closed();
	//this->deleteLater();
}

void CRecoveryLogWnd::OnDblClick(QTreeWidgetItem* pItem)
{
	ShellExecute(NULL, NULL, L"explorer.exe", (L"/select,\"" + pItem->text(2).toStdWString() + L"\"").c_str(), NULL, SW_SHOWNORMAL);
}

void CSandMan::OnRecoveryLog()
{
	if (!m_pRecoveryLogWnd->isVisible()) {
		bool bAlwaysOnTop = theConf->GetBool("Options/AlwaysOnTop", false);
		m_pRecoveryLogWnd->setWindowFlag(Qt::WindowStaysOnTopHint, bAlwaysOnTop);
		SafeShow(m_pRecoveryLogWnd);
	}
}