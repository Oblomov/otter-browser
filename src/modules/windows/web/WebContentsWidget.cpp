/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2017 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2015 Jan Bajer aka bajasoft <jbajer@gmail.com>
* Copyright (C) 2017 Piotr Wójcik <chocimier@tlen.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "WebContentsWidget.h"
#include "PasswordBarWidget.h"
#include "PermissionBarWidget.h"
#include "PopupsBarWidget.h"
#include "ProgressBarWidget.h"
#include "SearchBarWidget.h"
#include "SelectPasswordDialog.h"
#include "StartPageWidget.h"
#include "../../../core/AddonsManager.h"
#include "../../../core/Application.h"
#include "../../../core/Console.h"
#include "../../../core/GesturesManager.h"
#include "../../../core/InputInterpreter.h"
#include "../../../core/NetworkManagerFactory.h"
#include "../../../core/SearchEnginesManager.h"
#include "../../../core/SettingsManager.h"
#include "../../../core/ThemesManager.h"
#include "../../../core/ToolBarsManager.h"
#include "../../../core/Utils.h"
#include "../../../core/WebBackend.h"
#include "../../../ui/CertificateDialog.h"
#include "../../../ui/ContentsDialog.h"
#include "../../../ui/MainWindow.h"
#include "../../../ui/Menu.h"
#include "../../../ui/ReloadTimeDialog.h"
#include "../../../ui/SourceViewerWebWidget.h"
#include "../../../ui/WebsiteInformationDialog.h"
#include "../../../ui/Window.h"

#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>

namespace Otter
{

QString WebContentsWidget::m_sharedQuickFindQuery = nullptr;
QMap<int, QPixmap> WebContentsWidget::m_scrollCursors;

WebContentsWidget::WebContentsWidget(const QVariantMap &parameters, const QHash<int, QVariant> &options, WebWidget *widget, Window *window) : ContentsWidget(parameters, window),
	m_websiteInformationDialog(nullptr),
	m_layout(new QVBoxLayout(this)),
	m_splitter(new QSplitter(Qt::Vertical, this)),
	m_webWidget(nullptr),
	m_window(window),
	m_startPageWidget(nullptr),
	m_searchBarWidget(nullptr),
	m_progressBarWidget(nullptr),
	m_passwordBarWidget(nullptr),
	m_popupsBarWidget(nullptr),
	m_scrollMode(NoScroll),
	m_createStartPageTimer(0),
	m_quickFindTimer(0),
	m_scrollTimer(0),
	m_isTabPreferencesMenuVisible(false),
	m_isStartPageEnabled(!isSidebarPanel() && SettingsManager::getOption(SettingsManager::StartPage_EnableStartPageOption).toBool()),
	m_isIgnoringMouseRelease(false)
{
	m_splitter->hide();

	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);
	m_layout->addWidget(m_splitter);

	setLayout(m_layout);
	setFocusPolicy(Qt::StrongFocus);
	setStyleSheet(QLatin1String("workaround"));
	setWidget(widget, parameters, options);
}

void WebContentsWidget::timerEvent(QTimerEvent *event)
{
	if (event->timerId() == m_createStartPageTimer)
	{
		killTimer(m_createStartPageTimer);

		m_createStartPageTimer = 0;

		handleUrlChange(m_webWidget->getRequestedUrl());
	}
	else if (event->timerId() == m_quickFindTimer && m_searchBarWidget)
	{
		killTimer(m_quickFindTimer);

		m_quickFindTimer = 0;

		m_searchBarWidget->hide();
	}
	else if (event->timerId() == m_scrollTimer)
	{
		const QPoint scrollDelta((QCursor::pos() - m_beginCursorPosition) / 20);
		ScrollDirections directions(NoDirection);

		if (scrollDelta.x() < 0)
		{
			directions |= LeftDirection;
		}
		else if (scrollDelta.x() > 0)
		{
			directions |= RightDirection;
		}

		if (scrollDelta.y() < 0)
		{
			directions |= TopDirection;
		}
		else if (scrollDelta.y() > 0)
		{
			directions |= BottomDirection;
		}

		if (!m_scrollCursors.contains(directions))
		{
			if (directions == (BottomDirection | LeftDirection))
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-bottom-left.png"));
			}
			else if (directions == (BottomDirection | RightDirection))
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-bottom-right.png"));
			}
			else if (directions == BottomDirection)
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-bottom.png"));
			}
			else if (directions == LeftDirection)
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-left.png"));
			}
			else if (directions == RightDirection)
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-right.png"));
			}
			else if (directions == (TopDirection | LeftDirection))
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-top-left.png"));
			}
			else if (directions == (TopDirection | RightDirection))
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-top-right.png"));
			}
			else if (directions == TopDirection)
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-top.png"));
			}
			else if (directions == NoDirection)
			{
				m_scrollCursors[directions] = QPixmap(QLatin1String(":/cursors/scroll-vertical.png"));
			}
		}

		scrollContents(scrollDelta);

		QApplication::changeOverrideCursor(m_scrollCursors[directions]);
	}

	ContentsWidget::timerEvent(event);
}

void WebContentsWidget::showEvent(QShowEvent *event)
{
	ContentsWidget::showEvent(event);

	if (m_window && !m_progressBarWidget && getLoadingState() == WebWidget::OngoingLoadingState && ToolBarsManager::getToolBarDefinition(ToolBarsManager::ProgressBar).normalVisibility == ToolBarsManager::AlwaysVisibleToolBar)
	{
		m_progressBarWidget = new ProgressBarWidget(m_window, m_webWidget);
	}
}

void WebContentsWidget::focusInEvent(QFocusEvent *event)
{
	ContentsWidget::focusInEvent(event);

	if (m_startPageWidget && m_startPageWidget->isVisible())
	{
		m_startPageWidget->setFocus();
	}
	else
	{
		m_webWidget->setFocus();
	}
}

void WebContentsWidget::resizeEvent(QResizeEvent *event)
{
	ContentsWidget::resizeEvent(event);

	if (m_progressBarWidget)
	{
		m_progressBarWidget->scheduleGeometryUpdate();
	}
}

void WebContentsWidget::contextMenuEvent(QContextMenuEvent *event)
{
	if (m_scrollMode == MoveScroll || m_scrollMode == DragScroll)
	{
		event->accept();
	}
}

void WebContentsWidget::keyPressEvent(QKeyEvent *event)
{
	ContentsWidget::keyPressEvent(event);

	if (m_scrollMode == MoveScroll)
	{
		triggerAction(ActionsManager::EndScrollAction);

		event->accept();

		return;
	}

	if (event->key() == Qt::Key_Escape)
	{
		if (m_webWidget->getLoadingState() == WebWidget::OngoingLoadingState)
		{
			triggerAction(ActionsManager::StopAction);

			Application::triggerAction(ActionsManager::ActivateAddressFieldAction, {}, this);

			event->accept();
		}
		else if (!m_quickFindQuery.isEmpty())
		{
			m_quickFindQuery = QString();

			m_webWidget->findInPage(QString());

			event->accept();
		}
		else if (m_webWidget->hasSelection())
		{
			m_webWidget->triggerAction(ActionsManager::UnselectAction);

			event->accept();
		}
		else
		{
			MainWindow *mainWindow(MainWindow::findMainWindow(this));

			if (mainWindow && mainWindow->isFullScreen())
			{
				mainWindow->triggerAction(ActionsManager::FullScreenAction);
			}
		}
	}
}

void WebContentsWidget::mousePressEvent(QMouseEvent *event)
{
	if (m_scrollMode == MoveScroll || m_scrollMode == DragScroll)
	{
		event->accept();

		return;
	}
}

void WebContentsWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_isIgnoringMouseRelease)
	{
		m_isIgnoringMouseRelease = false;

		event->accept();

		return;
	}

	if (m_scrollMode == MoveScroll || m_scrollMode == DragScroll)
	{
		triggerAction(ActionsManager::EndScrollAction);
	}

	ContentsWidget::mouseReleaseEvent(event);
}

void WebContentsWidget::mouseMoveEvent(QMouseEvent *event)
{
	Q_UNUSED(event)

	if (m_scrollMode == DragScroll)
	{
		scrollContents(m_lastCursorPosition - QCursor::pos());

		m_lastCursorPosition = QCursor::pos();
	}
}

void WebContentsWidget::search(const QString &search, const QString &query)
{
	if (m_webWidget->getUrl().scheme() == QLatin1String("view-source"))
	{
		QVariantMap parameters;

		if (isPrivate())
		{
			parameters[QLatin1String("hints")] = SessionsManager::PrivateOpen;
		}

		setWidget(nullptr, parameters, (m_webWidget ? m_webWidget->getOptions() : QHash<int, QVariant>()));
	}

	m_webWidget->search(search, query);
}

void WebContentsWidget::print(QPrinter *printer)
{
	m_webWidget->print(printer);
}

void WebContentsWidget::goToHistoryIndex(int index)
{
	m_webWidget->goToHistoryIndex(index);
}

void WebContentsWidget::removeHistoryIndex(int index, bool purge)
{
	m_webWidget->removeHistoryIndex(index, purge);
}

void WebContentsWidget::triggerAction(int identifier, const QVariantMap &parameters)
{
	switch (identifier)
	{
		case ActionsManager::OpenSelectionAsLinkAction:
			{
				const QString text(m_webWidget->getSelectedText().trimmed());

				if (!text.isEmpty())
				{
					const InputInterpreter::InterpreterResult result(InputInterpreter::interpret(text, (InputInterpreter::NoBookmarkKeywordsFlag | InputInterpreter::NoHostLookupFlag | InputInterpreter::NoSearchKeywordsFlag)));

					if (result.isValid())
					{
						SessionsManager::OpenHints hints(SessionsManager::calculateOpenHints());

						if (parameters.contains(QLatin1String("hints")))
						{
							hints = SessionsManager::calculateOpenHints(parameters);
						}

						switch (result.type)
						{
							case InputInterpreter::InterpreterResult::UrlType:
								if (hints.testFlag(SessionsManager::CurrentTabOpen) && hints.testFlag(SessionsManager::PrivateOpen) == isPrivate())
								{
									setUrl(result.url);
								}
								else
								{
									MainWindow *mainWindow(MainWindow::findMainWindow(this));

									if (mainWindow)
									{
										mainWindow->triggerAction(ActionsManager::OpenUrlAction, {{QLatin1String("url"), result.url}, {QLatin1String("hints"), QVariant(hints)}});
									}
								}

								break;
							case InputInterpreter::InterpreterResult::SearchType:
								emit requestedSearch(result.searchQuery, result.searchEngine, hints);

								break;
							default:
								break;
						}
					}
				}
			}

			break;
		case ActionsManager::FillPasswordAction:
			{
				const QVector<PasswordsManager::PasswordInformation> passwords(PasswordsManager::getPasswords(getUrl(), PasswordsManager::FormPassword));

				if (!passwords.isEmpty())
				{
					if (passwords.count() == 1)
					{
						m_webWidget->fillPassword(passwords.first());
					}
					else
					{
						SelectPasswordDialog dialog(passwords, this);

						if (dialog.exec() == QDialog::Accepted)
						{
							m_webWidget->fillPassword(dialog.getPassword());
						}
					}
				}
			}

			break;
		case ActionsManager::GoToParentDirectoryAction:
			{
				const QUrl url(getUrl());

				if (url.toString().endsWith(QLatin1Char('/')))
				{
					setUrl(url.resolved(QUrl(QLatin1String(".."))));
				}
				else
				{
					setUrl(url.resolved(QUrl(QLatin1String("."))));
				}
			}

			break;
		case ActionsManager::ScheduleReloadAction:
			setOption(SettingsManager::Content_PageReloadTimeOption, parameters.value(QLatin1String("time"), QLatin1String("custom")));

			break;
		case ActionsManager::PasteAndGoAction:
			if (!QGuiApplication::clipboard()->text().isEmpty())
			{
				const InputInterpreter::InterpreterResult result(InputInterpreter::interpret(QGuiApplication::clipboard()->text().trimmed(), (InputInterpreter::NoBookmarkKeywordsFlag | InputInterpreter::NoHostLookupFlag | InputInterpreter::NoSearchKeywordsFlag)));

				if (result.isValid())
				{
					SessionsManager::OpenHints hints(SettingsManager::getOption(SettingsManager::Browser_OpenLinksInNewTabOption).toBool() ? SessionsManager::NewTabOpen : SessionsManager::CurrentTabOpen);

					if (parameters.contains(QLatin1String("hints")))
					{
						hints = SessionsManager::calculateOpenHints(parameters);
					}

					switch (result.type)
					{
						case InputInterpreter::InterpreterResult::UrlType:
							if (hints.testFlag(SessionsManager::CurrentTabOpen) && hints.testFlag(SessionsManager::PrivateOpen) == isPrivate())
							{
								setUrl(result.url);
							}
							else
							{
								MainWindow *mainWindow(MainWindow::findMainWindow(this));

								if (mainWindow)
								{
									mainWindow->triggerAction(ActionsManager::OpenUrlAction, {{QLatin1String("url"), result.url}, {QLatin1String("hints"), QVariant(hints)}});
								}
							}

							break;
						case InputInterpreter::InterpreterResult::SearchType:
							emit requestedSearch(result.searchQuery, result.searchEngine, hints);

							break;
						default:
							break;
					}
				}
			}

			break;
		case ActionsManager::FindAction:
		case ActionsManager::QuickFindAction:
			if (!m_searchBarWidget)
			{
				m_searchBarWidget = new SearchBarWidget(this);
				m_searchBarWidget->hide();

				m_layout->insertWidget(0, m_searchBarWidget);

				connect(m_searchBarWidget, SIGNAL(requestedSearch(WebWidget::FindFlags)), this, SLOT(findInPage(WebWidget::FindFlags)));
				connect(m_searchBarWidget, SIGNAL(flagsChanged(WebWidget::FindFlags)), this, SLOT(updateFindHighlight(WebWidget::FindFlags)));

				if (SettingsManager::getOption(SettingsManager::Search_EnableFindInPageAsYouTypeOption).toBool())
				{
					connect(m_searchBarWidget, SIGNAL(queryChanged()), this, SLOT(findInPage()));
				}
			}

			if (!m_searchBarWidget->isVisible())
			{
				if (identifier == ActionsManager::QuickFindAction)
				{
					killTimer(m_quickFindTimer);

					m_quickFindTimer = startTimer(2000);
				}

				if (SettingsManager::getOption(SettingsManager::Search_ReuseLastQuickFindQueryOption).toBool())
				{
					m_searchBarWidget->setQuery(m_sharedQuickFindQuery);
				}

				m_searchBarWidget->show();
			}

			m_searchBarWidget->selectAll();

			break;
		case ActionsManager::FindNextAction:
		case ActionsManager::FindPreviousAction:
			{
				WebWidget::FindFlags flags(m_searchBarWidget ? m_searchBarWidget->getFlags() : WebWidget::NoFlagsFind);

				if (identifier == ActionsManager::FindPreviousAction)
				{
					flags |= WebWidget::BackwardFind;
				}

				findInPage(flags);
			}

			break;
		case ActionsManager::SearchAction:
			{
				const SearchEnginesManager::SearchEngineDefinition searchEngine(SearchEnginesManager::getSearchEngine(parameters.value(QLatin1String("searchEngine"), getOption(SettingsManager::Search_DefaultQuickSearchEngineOption)).toString()));

				if (!searchEngine.isValid())
				{
					break;
				}

				const SessionsManager::OpenHints hints(SessionsManager::calculateOpenHints());
				const QString query(parameters.contains(QLatin1String("query")) ? parameters.value(QLatin1String("query")).toString() : parseQuery(parameters.value(QLatin1String("queryPlaceholder"), QLatin1String("{selection}")).toString()));

				if (hints == SessionsManager::CurrentTabOpen)
				{
					search(query, searchEngine.identifier);
				}
				else
				{
					emit requestedSearch(query, searchEngine.identifier, hints);
				}

				setOption(SettingsManager::Search_DefaultQuickSearchEngineOption, searchEngine.identifier);
			}

			break;
		case ActionsManager::ZoomInAction:
			setZoom(qMin((getZoom() + 10), 10000));

			break;
		case ActionsManager::ZoomOutAction:
			setZoom(qMax((getZoom() - 10), 10));

			break;
		case ActionsManager::ZoomOriginalAction:
			setZoom(100);

			break;
		case ActionsManager::StartDragScrollAction:
			if (m_scrollMode == DragScroll)
			{
				setScrollMode(NoScroll);
			}
			else
			{
				setScrollMode(DragScroll);
			}

			break;
		case ActionsManager::StartMoveScrollAction:
			if (m_scrollMode == MoveScroll)
			{
				setScrollMode(NoScroll);
			}
			else
			{
				setScrollMode(MoveScroll);
			}

			break;
		case ActionsManager::EndScrollAction:
			setScrollMode(NoScroll);

			break;
		case ActionsManager::PrintAction:
		case ActionsManager::PrintPreviewAction:
		case ActionsManager::BookmarkPageAction:
			ContentsWidget::triggerAction(identifier, parameters);

			break;
		case ActionsManager::EnableJavaScriptAction:
			m_webWidget->setOption(SettingsManager::Permissions_EnableJavaScriptOption, parameters.value(QLatin1String("isChecked"), !getActionState(identifier, parameters).isChecked).toBool());

			break;
		case ActionsManager::EnableReferrerAction:
			m_webWidget->setOption(SettingsManager::Network_EnableReferrerOption, parameters.value(QLatin1String("isChecked"), !getActionState(identifier, parameters).isChecked).toBool());

			break;
		case ActionsManager::QuickPreferencesAction:
			{
				if (m_isTabPreferencesMenuVisible)
				{
					return;
				}

				m_isTabPreferencesMenuVisible = true;

				Menu menu(Menu::NoMenuRole, this);
				menu.load(QLatin1String("menu/quickPreferences.json"), {}, ActionExecutor::Object(this, this));
				menu.exec(QCursor::pos());

				m_isTabPreferencesMenuVisible = false;
			}

			break;
		case ActionsManager::ResetQuickPreferencesAction:
			m_webWidget->clearOptions();

			break;
		case ActionsManager::WebsiteInformationAction:
			if (!Utils::isUrlEmpty(m_webWidget->getUrl()))
			{
				if (m_websiteInformationDialog)
				{
					m_websiteInformationDialog->close();
					m_websiteInformationDialog->deleteLater();
					m_websiteInformationDialog = nullptr;
				}
				else
				{
					m_websiteInformationDialog = new WebsiteInformationDialog(m_webWidget, this);

					ContentsDialog *dialog(new ContentsDialog(ThemesManager::createIcon(QLatin1String("dialog-information")), m_websiteInformationDialog->windowTitle(), QString(), QString(), QDialogButtonBox::NoButton, m_websiteInformationDialog, this));

					connect(m_websiteInformationDialog, SIGNAL(finished(int)), dialog, SLOT(close()));

					showDialog(dialog, false);
				}
			}

			break;
		case ActionsManager::WebsiteCertificateInformationAction:
			if (!m_webWidget->getSslInformation().certificates.isEmpty())
			{
				CertificateDialog *dialog(new CertificateDialog(m_webWidget->getSslInformation().certificates));
				dialog->setAttribute(Qt::WA_DeleteOnClose);
				dialog->show();
			}

			break;
		default:
			if (m_startPageWidget)
			{
				switch (identifier)
				{
					case ActionsManager::OpenLinkAction:
					case ActionsManager::OpenLinkInCurrentTabAction:
					case ActionsManager::OpenLinkInNewTabAction:
					case ActionsManager::OpenLinkInNewTabBackgroundAction:
					case ActionsManager::OpenLinkInNewWindowAction:
					case ActionsManager::OpenLinkInNewWindowBackgroundAction:
					case ActionsManager::OpenLinkInNewPrivateTabAction:
					case ActionsManager::OpenLinkInNewPrivateTabBackgroundAction:
					case ActionsManager::OpenLinkInNewPrivateWindowAction:
					case ActionsManager::OpenLinkInNewPrivateWindowBackgroundAction:
					case ActionsManager::ContextMenuAction:
						m_startPageWidget->triggerAction(identifier, parameters);

						return;
					default:
						break;
				}
			}

			m_webWidget->triggerAction(identifier, parameters);

			break;
	}
}

void WebContentsWidget::findInPage(WebWidget::FindFlags flags)
{
	if (m_quickFindTimer != 0)
	{
		killTimer(m_quickFindTimer);

		m_quickFindTimer = startTimer(2000);
	}

	if (flags == WebWidget::NoFlagsFind)
	{
		flags = (m_searchBarWidget ? m_searchBarWidget->getFlags() : WebWidget::HighlightAllFind);
	}

	m_quickFindQuery = (m_searchBarWidget ? m_searchBarWidget->getQuery() : m_sharedQuickFindQuery);

	const bool hasFound(m_webWidget->findInPage(m_quickFindQuery, flags) != 0);

	if (!m_quickFindQuery.isEmpty() && !isPrivate() && SettingsManager::getOption(SettingsManager::Search_ReuseLastQuickFindQueryOption).toBool())
	{
		m_sharedQuickFindQuery = m_quickFindQuery;
	}

	if (m_searchBarWidget)
	{
		m_searchBarWidget->setResultsFound(hasFound);
	}
}

void WebContentsWidget::closePasswordBar()
{
	if (m_passwordBarWidget)
	{
		m_passwordBarWidget->hide();
		m_passwordBarWidget->deleteLater();
		m_passwordBarWidget = nullptr;
	}
}

void WebContentsWidget::closePopupsBar()
{
	if (m_popupsBarWidget)
	{
		m_popupsBarWidget->hide();
		m_popupsBarWidget->deleteLater();
		m_popupsBarWidget = nullptr;
	}
}

void WebContentsWidget::scrollContents(const QPoint &delta)
{
	if (m_startPageWidget)
	{
		m_startPageWidget->scrollContents(delta);
	}
	else if (m_webWidget)
	{
		m_webWidget->setScrollPosition(m_webWidget->getScrollPosition() + delta);
	}
}

void WebContentsWidget::handleOptionChanged(int identifier, const QVariant &value)
{
	switch (identifier)
	{
		case SettingsManager::Search_EnableFindInPageAsYouTypeOption:
			if (m_searchBarWidget)
			{
				if (value.toBool())
				{
					connect(m_searchBarWidget, SIGNAL(queryChanged(QString)), this, SLOT(findInPage()));
				}
				else
				{
					disconnect(m_searchBarWidget, SIGNAL(queryChanged(QString)), this, SLOT(findInPage()));
				}
			}

			break;
		case SettingsManager::StartPage_EnableStartPageOption:
			if (!isSidebarPanel())
			{
				m_isStartPageEnabled = value.toBool();
			}

			break;
		default:
			break;
	}
}

void WebContentsWidget::handleUrlChange(const QUrl &url)
{
	if (m_passwordBarWidget && m_passwordBarWidget->shouldClose(url))
	{
		closePasswordBar();
	}

	if (!m_window)
	{
		return;
	}

	const bool showStartPage((m_isStartPageEnabled && Utils::isUrlEmpty(url)) || url == QUrl(QLatin1String("about:start")));

	if (showStartPage)
	{
		if (m_webWidget)
		{
			m_webWidget->setParent(this);
			m_webWidget->hide();
		}

		m_splitter->hide();

		if (!m_startPageWidget)
		{
			m_startPageWidget = new StartPageWidget(m_window);
			m_startPageWidget->setParent(this);
		}

		if (m_layout->indexOf(m_startPageWidget) < 0)
		{
			layout()->addWidget(m_startPageWidget);
		}

		if (GesturesManager::isTracking() && GesturesManager::getTrackedObject() == m_webWidget->getViewport())
		{
			GesturesManager::continueGesture(m_startPageWidget);
		}

		m_startPageWidget->show();
	}
	else
	{
		if (m_startPageWidget)
		{
			m_webWidget->setParent(this);
			m_startPageWidget->hide();
		}

		if (m_webWidget)
		{
			m_splitter->show();
			m_splitter->insertWidget(0, m_webWidget);

			m_webWidget->show();

			if (m_window && m_window->isActive() && !Utils::isUrlEmpty(m_webWidget->getUrl()))
			{
				m_webWidget->setFocus();
			}
		}

		if (m_startPageWidget)
		{
			if (GesturesManager::isTracking() && GesturesManager::getTrackedObject() == m_startPageWidget && m_webWidget)
			{
				GesturesManager::continueGesture(m_webWidget->getViewport());
			}

			layout()->removeWidget(m_startPageWidget);

			m_startPageWidget->hide();
			m_startPageWidget->markForDeletion();
			m_startPageWidget = nullptr;

			if (GesturesManager::isTracking() && GesturesManager::getTrackedObject() == m_startPageWidget && m_webWidget)
			{
				GesturesManager::continueGesture(m_webWidget->getViewport());
			}

			if (m_webWidget && m_window && m_window->isActive())
			{
				m_webWidget->setFocus();
			}
		}
	}
}

void WebContentsWidget::handleSavePasswordRequest(const PasswordsManager::PasswordInformation &password, bool isUpdate)
{
	if (!m_passwordBarWidget)
	{
		bool isValid(false);

		for (int i = 0; i < password.fields.count(); ++i)
		{
			if (password.fields.at(i).type == PasswordsManager::PasswordField && !password.fields.at(i).value.isEmpty())
			{
				isValid = true;

				break;
			}
		}

		if (isValid)
		{
			m_passwordBarWidget = new PasswordBarWidget(password, isUpdate, this);

			connect(m_passwordBarWidget, SIGNAL(requestedClose()), this, SLOT(closePasswordBar()));

			m_layout->insertWidget(0, m_passwordBarWidget);

			m_passwordBarWidget->show();
		}
	}
}

void WebContentsWidget::handlePopupWindowRequest(const QUrl &parentUrl, const QUrl &popupUrl)
{
	if (!m_popupsBarWidget)
	{
		m_popupsBarWidget = new PopupsBarWidget(parentUrl, isPrivate(), this);

		connect(m_popupsBarWidget, SIGNAL(requestedClose()), this, SLOT(closePopupsBar()));

		m_layout->insertWidget(0, m_popupsBarWidget);

		m_popupsBarWidget->show();
	}

	m_popupsBarWidget->addPopup(popupUrl);
}

void WebContentsWidget::handlePermissionRequest(WebWidget::FeaturePermission feature, const QUrl &url, bool cancel)
{
	if (!url.isValid())
	{
		return;
	}

	if (cancel)
	{
		for (int i = 0; i < m_permissionBarWidgets.count(); ++i)
		{
			if (m_permissionBarWidgets.at(i)->getFeature() == feature && m_permissionBarWidgets.at(i)->getUrl() == url)
			{
				m_layout->removeWidget(m_permissionBarWidgets.at(i));

				m_permissionBarWidgets.at(i)->deleteLater();

				m_permissionBarWidgets.removeAt(i);

				break;
			}
		}
	}
	else
	{
		for (int i = 0; i < m_permissionBarWidgets.count(); ++i)
		{
			if (m_permissionBarWidgets.at(i)->getFeature() == feature && m_permissionBarWidgets.at(i)->getUrl() == url)
			{
				return;
			}
		}

		PermissionBarWidget *widget(new PermissionBarWidget(feature, url, this));

		m_layout->insertWidget((m_permissionBarWidgets.count() + (m_searchBarWidget ? 1 : 0)), widget);

		widget->show();

		m_permissionBarWidgets.append(widget);

		emit needsAttention();

		connect(widget, SIGNAL(permissionChanged(WebWidget::PermissionPolicies)), this, SLOT(notifyPermissionChanged(WebWidget::PermissionPolicies)));
	}
}

void WebContentsWidget::handleInspectorVisibilityChangeRequest(bool isVisible)
{
	if (m_webWidget)
	{
		QWidget *inspector(m_webWidget->getInspector());

		if (inspector)
		{
			if (isVisible)
			{
				if (m_splitter->indexOf(inspector) < 0)
				{
					m_splitter->addWidget(inspector);
				}

				inspector->show();
			}
			else
			{
				inspector->hide();
			}
		}
	}
}

void WebContentsWidget::handleLoadingStateChange(WebWidget::LoadingState state)
{
	if (state == WebWidget::CrashedLoadingState)
	{
		const QString tabCrashingAction(SettingsManager::getOption(SettingsManager::Interface_TabCrashingActionOption, getUrl()).toString());
		bool reloadTab(tabCrashingAction != QLatin1String("close"));

		if (tabCrashingAction == QLatin1String("ask"))
		{
			ContentsDialog dialog(ThemesManager::createIcon(QLatin1String("dialog-warning")), tr("Question"), tr("This tab has crashed."), tr("Do you want to try to reload it?"), (QDialogButtonBox::Yes | QDialogButtonBox::Close), nullptr, this);
			dialog.setCheckBox(tr("Do not show this message again"), false);

			showDialog(&dialog);

			if (dialog.getCheckBoxState())
			{
				SettingsManager::setOption(SettingsManager::Interface_TabCrashingActionOption, (dialog.isAccepted() ? QLatin1String("reload") : QLatin1String("close")));
			}

			reloadTab = dialog.isAccepted();
		}

		if (reloadTab)
		{
			triggerAction(ActionsManager::ReloadAction);
		}
		else if (m_window)
		{
			m_window->requestClose();
		}
	}
	else if (m_window && !m_progressBarWidget && state == WebWidget::OngoingLoadingState && ToolBarsManager::getToolBarDefinition(ToolBarsManager::ProgressBar).normalVisibility == ToolBarsManager::AutoVisibilityToolBar)
	{
		m_progressBarWidget = new ProgressBarWidget(m_window, m_webWidget);
	}
}

void WebContentsWidget::notifyPermissionChanged(WebWidget::PermissionPolicies policies)
{
	PermissionBarWidget *widget(qobject_cast<PermissionBarWidget*>(sender()));

	if (widget)
	{
		m_webWidget->setPermission(widget->getFeature(), widget->getUrl(), policies);

		m_permissionBarWidgets.removeAll(widget);

		m_layout->removeWidget(widget);

		widget->deleteLater();
	}
}

void WebContentsWidget::notifyRequestedNewWindow(WebWidget *widget, SessionsManager::OpenHints hints)
{
	if (isPrivate())
	{
		hints |= SessionsManager::PrivateOpen;
	}

	emit requestedNewWindow(new WebContentsWidget({{QLatin1String("hints"), QVariant(hints)}}, widget->getOptions(), widget, nullptr), hints);
}

void WebContentsWidget::updateFindHighlight(WebWidget::FindFlags flags)
{
	if (m_searchBarWidget)
	{
		m_webWidget->findInPage(QString(), (flags | WebWidget::HighlightAllFind));
		m_webWidget->findInPage(m_searchBarWidget->getQuery(), flags);
	}
}

void WebContentsWidget::setScrollMode(ScrollMode mode)
{
	if (m_scrollMode == NoScroll && mode != NoScroll)
	{
		m_webWidget->installEventFilter(this);
	}
	else if (m_scrollMode != NoScroll && mode == NoScroll)
	{
		m_webWidget->removeEventFilter(this);
	}

	m_scrollMode = mode;

	switch (mode)
	{
		case MoveScroll:
			GesturesManager::cancelGesture();

			if (!m_scrollCursors.contains(NoDirection))
			{
				m_scrollCursors[NoDirection] = QPixmap(QLatin1String(":/cursors/scroll-vertical.png"));
			}

			if (m_scrollTimer == 0)
			{
				m_scrollTimer = startTimer(10);
			}

			grabKeyboard();
			grabMouse();

			m_isIgnoringMouseRelease = true;

			QApplication::setOverrideCursor(m_scrollCursors[NoDirection]);

			break;
		case DragScroll:
			grabMouse();

			QApplication::setOverrideCursor(Qt::ClosedHandCursor);

			break;
		default:
			QApplication::restoreOverrideCursor();

			break;
	}

	if (mode == NoScroll)
	{
		m_beginCursorPosition = QPoint();
		m_lastCursorPosition = QPoint();

		if (m_scrollTimer > 0)
		{
			killTimer(m_scrollTimer);

			m_scrollTimer = 0;

			releaseKeyboard();
		}

		releaseMouse();
	}
	else
	{
		m_beginCursorPosition = QCursor::pos();
		m_lastCursorPosition = QCursor::pos();
	}
}

void WebContentsWidget::setWidget(WebWidget *widget, const QVariantMap &parameters, const QHash<int, QVariant> &options)
{
	if (m_webWidget)
	{
		disconnect(m_webWidget, SIGNAL(requestedCloseWindow()));

		m_webWidget->hide();
		m_webWidget->close();
		m_webWidget->deleteLater();

		handleLoadingStateChange(WebWidget::FinishedLoadingState);
	}

	if (widget)
	{
		widget->setParent(this);
	}
	else
	{
		const QString webBackendName(parameters.value(QLatin1String("webBackend")).toString());
		WebBackend *webBackend(AddonsManager::getWebBackend(webBackendName));

		if (!webBackend)
		{
			webBackend = AddonsManager::getWebBackend();

			if (!webBackendName.isEmpty())
			{
				Console::addMessage(tr("Failed to load requested web backend: %1").arg(webBackendName), Console::OtherCategory, Console::WarningLevel, QString(), -1, (m_window ? m_window->getIdentifier() : 0));
			}
		}

		widget = webBackend->createWidget(parameters, this);

		if (m_window)
		{
			m_createStartPageTimer = startTimer(50);
		}

		connect(m_splitter, SIGNAL(splitterMoved(int,int)), widget, SIGNAL(geometryChanged()));
	}

	bool isHidden(m_isStartPageEnabled && Utils::isUrlEmpty(widget->getUrl()) && (!m_webWidget || (m_startPageWidget && m_startPageWidget->isVisibleTo(this))));

	m_webWidget = widget;
	m_webWidget->setOptions(options);

	if (isHidden)
	{
		m_webWidget->hide();
	}
	else
	{
		m_splitter->show();
		m_splitter->insertWidget(0, m_webWidget);

		m_webWidget->show();
	}

	if (m_window)
	{
		widget->setWindowIdentifier(m_window->getIdentifier());

		connect(m_webWidget, SIGNAL(requestedCloseWindow()), m_window, SLOT(requestClose()));
	}

	handleLoadingStateChange(m_webWidget->getLoadingState());

	connect(SettingsManager::getInstance(), &SettingsManager::optionChanged, this, &WebContentsWidget::handleOptionChanged);
	connect(m_webWidget, &WebWidget::aboutToNavigate, this, &WebContentsWidget::aboutToNavigate);
	connect(m_webWidget, &WebWidget::aboutToNavigate, this, &WebContentsWidget::closePopupsBar);
	connect(m_webWidget, &WebWidget::needsAttention, this, &WebContentsWidget::needsAttention);
	connect(m_webWidget, &WebWidget::requestedNewWindow, this, &WebContentsWidget::notifyRequestedNewWindow);
	connect(m_webWidget, &WebWidget::requestedSearch, this, &WebContentsWidget::requestedSearch);
	connect(m_webWidget, &WebWidget::requestedPopupWindow, this, &WebContentsWidget::handlePopupWindowRequest);
	connect(m_webWidget, &WebWidget::requestedPermission, this, &WebContentsWidget::handlePermissionRequest);
	connect(m_webWidget, &WebWidget::requestedSavePassword, this, &WebContentsWidget::handleSavePasswordRequest);
	connect(m_webWidget, &WebWidget::requestedGeometryChange, this, &WebContentsWidget::requestedGeometryChange);
	connect(m_webWidget, &WebWidget::requestedInspectorVisibilityChange, this, &WebContentsWidget::handleInspectorVisibilityChangeRequest);
	connect(m_webWidget, &WebWidget::statusMessageChanged, this, &WebContentsWidget::statusMessageChanged);
	connect(m_webWidget, &WebWidget::titleChanged, this, &WebContentsWidget::titleChanged);
	connect(m_webWidget, &WebWidget::urlChanged, this, &WebContentsWidget::urlChanged);
	connect(m_webWidget, &WebWidget::urlChanged, this, &WebContentsWidget::handleUrlChange);
	connect(m_webWidget, &WebWidget::iconChanged, this, &WebContentsWidget::iconChanged);
	connect(m_webWidget, &WebWidget::requestBlocked, this, &WebContentsWidget::requestBlocked);
	connect(m_webWidget, SIGNAL(actionsStateChanged(QVector<int>)), this, SIGNAL(actionsStateChanged(QVector<int>)));
	connect(m_webWidget, SIGNAL(actionsStateChanged(ActionsManager::ActionDefinition::ActionCategories)), this, SIGNAL(actionsStateChanged(ActionsManager::ActionDefinition::ActionCategories)));
	connect(m_webWidget, &WebWidget::contentStateChanged, this, &WebContentsWidget::contentStateChanged);
	connect(m_webWidget, &WebWidget::loadingStateChanged, this, &WebContentsWidget::loadingStateChanged);
	connect(m_webWidget, &WebWidget::loadingStateChanged, this, &WebContentsWidget::handleLoadingStateChange);
	connect(m_webWidget, &WebWidget::pageInformationChanged, this, &WebContentsWidget::pageInformationChanged);
	connect(m_webWidget, &WebWidget::optionChanged, this, &WebContentsWidget::optionChanged);
	connect(m_webWidget, &WebWidget::zoomChanged, this, &WebContentsWidget::zoomChanged);

	emit webWidgetChanged();
}

void WebContentsWidget::setOption(int identifier, const QVariant &value)
{
	if (identifier == SettingsManager::Content_PageReloadTimeOption && value.type() == QVariant::String && value.toString() == QLatin1String("custom"))
	{
		ReloadTimeDialog dialog(qMax(0, getOption(SettingsManager::Content_PageReloadTimeOption).toInt()), this);

		if (dialog.exec() == QDialog::Accepted)
		{
			m_webWidget->setOption(SettingsManager::Content_PageReloadTimeOption, dialog.getReloadTime());
		}
	}
	else if (identifier == SettingsManager::Network_UserAgentOption && value.toString() == QLatin1String("custom"))
	{
		bool isConfirmed(false);
		const QString userAgent(QInputDialog::getText(this, tr("Select User Agent"), tr("Enter User Agent:"), QLineEdit::Normal, NetworkManagerFactory::getUserAgent(m_webWidget->getOption(SettingsManager::Network_UserAgentOption).toString()).value, &isConfirmed));

		if (isConfirmed)
		{
			m_webWidget->setOption(SettingsManager::Network_UserAgentOption, QLatin1String("custom;") + userAgent);
		}
	}
	else
	{
		m_webWidget->setOption(identifier, value);
	}
}

void WebContentsWidget::setHistory(const WindowHistoryInformation &history)
{
	if (history.entries.count() == 1 && QUrl(history.entries.at(0).url).scheme() == QLatin1String("view-source"))
	{
		setUrl(QUrl(history.entries.at(0).url), true);
	}
	else
	{
		m_webWidget->setHistory(history);
	}
}

void WebContentsWidget::setZoom(int zoom)
{
	m_webWidget->setZoom(zoom);
}

void WebContentsWidget::setUrl(const QUrl &url, bool isTyped)
{
	const QHash<int, QVariant> options(m_webWidget ? m_webWidget->getOptions() : QHash<int, QVariant>());
	QVariantMap parameters;

	if (isPrivate())
	{
		parameters[QLatin1String("hints")] = SessionsManager::PrivateOpen;
	}

	if (url.scheme() == QLatin1String("view-source") && m_webWidget->getUrl().scheme() != QLatin1String("view-source"))
	{
		setWidget(new SourceViewerWebWidget(isPrivate(), this), parameters, options);
	}
	else if (url.scheme() != QLatin1String("view-source") && m_webWidget->getUrl().scheme() == QLatin1String("view-source"))
	{
		setWidget(nullptr, parameters, options);
	}

	m_webWidget->setRequestedUrl(url, isTyped);

	if (m_window && m_window->isActive())
	{
		m_webWidget->setFocus();
	}
}

void WebContentsWidget::setParent(Window *window)
{
	ContentsWidget::setParent(window);

	m_window = window;

	if (m_progressBarWidget)
	{
		m_progressBarWidget->deleteLater();
		m_progressBarWidget = nullptr;
	}

	if (window && m_webWidget)
	{
		if (m_webWidget->getLoadingState() == WebWidget::OngoingLoadingState)
		{
			handleLoadingStateChange(WebWidget::OngoingLoadingState);
		}

		m_webWidget->setWindowIdentifier(window->getIdentifier());

		connect(m_webWidget, SIGNAL(requestedCloseWindow()), window, SLOT(requestClose()));
	}
}

WebContentsWidget* WebContentsWidget::clone(bool cloneHistory) const
{
	QVariantMap parameters;

	if (m_webWidget->isPrivate())
	{
		parameters[QLatin1String("hints")] = SessionsManager::PrivateOpen;
	}

	WebContentsWidget *webWidget(new WebContentsWidget(parameters, m_webWidget->getOptions(), m_webWidget->clone(cloneHistory), nullptr));
	webWidget->m_webWidget->setRequestedUrl(m_webWidget->getUrl(), false, true);

	return webWidget;
}

WebWidget* WebContentsWidget::getWebWidget() const
{
	return m_webWidget;
}

QString WebContentsWidget::parseQuery(const QString &query) const
{
	if (query.isEmpty())
	{
		return query;
	}

	QString mutableQuery(query);
	const QStringList placeholders({QLatin1String("clipboard"), QLatin1String("frameUrl"), QLatin1String("imageUrl"), QLatin1String("linkUrl"), QLatin1String("pageUrl"), QLatin1String("selection")});

	for (int i = 0; i < placeholders.count(); ++i)
	{
		const QString placeholder(QStringLiteral("{%1}").arg(placeholders.at(i)));

		if (mutableQuery.contains(placeholder))
		{
			if (placeholders.at(i) == QLatin1String("clipboard"))
			{
				mutableQuery.replace(placeholder, QGuiApplication::clipboard()->text());
			}
			else if (m_webWidget)
			{
				if (placeholders.at(i) == QLatin1String("frameUrl"))
				{
					mutableQuery.replace(placeholder, m_webWidget->getActiveFrame().url.toString());
				}
				else if (placeholders.at(i) == QLatin1String("imageUrl"))
				{
					mutableQuery.replace(placeholder, m_webWidget->getActiveImage().url.toString());
				}
				else if (placeholders.at(i) == QLatin1String("linkUrl"))
				{
					mutableQuery.replace(placeholder, m_webWidget->getActiveLink().url.toString());
				}
				else if (placeholders.at(i) == QLatin1String("pageUrl"))
				{
					mutableQuery.replace(placeholder, m_webWidget->getUrl().toString());
				}
				else
				{
					mutableQuery.replace(placeholder, m_webWidget->getSelectedText());
				}
			}
		}
	}

	return mutableQuery;
}

QString WebContentsWidget::getTitle() const
{
	return ((m_startPageWidget && m_startPageWidget->isVisibleTo(this)) ? tr("Start Page") : m_webWidget->getTitle());
}

QString WebContentsWidget::getDescription() const
{
	return ((m_startPageWidget && m_startPageWidget->isVisibleTo(this)) ? QString() : m_webWidget->getDescription());
}

QLatin1String WebContentsWidget::getType() const
{
	return QLatin1String("web");
}

QVariant WebContentsWidget::getOption(int identifier) const
{
	return m_webWidget->getOption(identifier);
}

QUrl WebContentsWidget::getUrl() const
{
	return m_webWidget->getRequestedUrl();
}

QIcon WebContentsWidget::getIcon() const
{
	return m_webWidget->getIcon();
}

QPixmap WebContentsWidget::createThumbnail()
{
	if (m_startPageWidget && m_startPageWidget->isVisibleTo(this))
	{
		return m_startPageWidget->createThumbnail();
	}

	return m_webWidget->createThumbnail();
}

ActionsManager::ActionDefinition::State WebContentsWidget::getActionState(int identifier, const QVariantMap &parameters) const
{
	return m_webWidget->getActionState(identifier, parameters);
}

WindowHistoryInformation WebContentsWidget::getHistory() const
{
	return m_webWidget->getHistory();
}

QHash<int, QVariant> WebContentsWidget::getOptions() const
{
	return m_webWidget->getOptions();
}

WebWidget::ContentStates WebContentsWidget::getContentState() const
{
	return m_webWidget->getContentState();
}

WebWidget::LoadingState WebContentsWidget::getLoadingState() const
{
	return m_webWidget->getLoadingState();
}

int WebContentsWidget::getZoom() const
{
	return m_webWidget->getZoom();
}

bool WebContentsWidget::canClone() const
{
	return true;
}

bool WebContentsWidget::canZoom() const
{
	return true;
}

bool WebContentsWidget::isPrivate() const
{
	return m_webWidget->isPrivate();
}

bool WebContentsWidget::eventFilter(QObject *object, QEvent *event)
{
	if (event->type() == QEvent::Wheel && m_scrollMode != NoScroll)
	{
		setScrollMode(NoScroll);
	}

	return QObject::eventFilter(object, event);
}

}
