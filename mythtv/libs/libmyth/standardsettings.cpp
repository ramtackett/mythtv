#include "standardsettings.h"
#include <QCoreApplication>

#include <mythcontext.h>
#include <mythmainwindow.h>
#include <mythdialogbox.h>
#include <mythuispinbox.h>
#include <mythuitext.h>
#include <mythuibutton.h>
#include <mythuifilebrowser.h>
#include "mythlogging.h"

void MythUIButtonListItemSetting::ShouldUpdate(StandardSetting *setting)
{
    setting->updateButton(this);
}

StandardSetting::StandardSetting(Storage *_storage) :
    m_enabled(true), m_label(""), m_helptext(""), m_visible(true),
    m_haveChanged(false),
    m_storage(_storage),
    m_parent(NULL)
{
}

StandardSetting::~StandardSetting()
{
    QList<StandardSetting *>::const_iterator i;
    for (i = m_children.constBegin(); i != m_children.constEnd(); ++i)
        delete *i;
    m_children.clear();

    QMap<QString, QList<StandardSetting *> >::const_iterator iMap;
    for (iMap = m_targets.constBegin(); iMap != m_targets.constEnd(); ++iMap)
    {
        for (i = (*iMap).constBegin(); i != (*iMap).constEnd(); ++i)
            delete *i;
    }
    m_targets.clear();
}

MythUIButtonListItem * StandardSetting::createButton(MythUIButtonList * list)
{
    MythUIButtonListItemSetting *item =
        new MythUIButtonListItemSetting(list, m_label);
    item->SetData(qVariantFromValue(this));
    connect(this, SIGNAL(ShouldRedraw(StandardSetting *)),
            item, SLOT(ShouldUpdate(StandardSetting *)));
    updateButton(item);
    return item;
}

void StandardSetting::setEnabled(bool b)
{
    m_enabled = b;
    emit ShouldRedraw(this);
}

void StandardSetting::setParent(StandardSetting *parent)
{
    m_parent = parent;
}

void StandardSetting::addChild(StandardSetting *child)
{
    if (!child)
        return;

    m_children.append(child);
    child->setParent(this);
}

bool StandardSetting::keyPressEvent(QKeyEvent *)
{
    return false;
}

/**
 * This method is called whenever the UI need to reflect a change
 * Reimplement this If you widget need a custom look
 * \param item is the associated MythUIButtonListItem to be updated
 */
void StandardSetting::updateButton(MythUIButtonListItem *item)
{
    item->DisplayState("standard", "widgettype");
    item->setEnabled(isEnabled());
    item->SetText(m_label);
    item->SetText(m_settingValue, "value");
    item->SetText(getHelpText(), "description");
    item->setDrawArrow(haveSubSettings());
}

void StandardSetting::addTargetedChild(const QString &value,
                                       StandardSetting * setting)
{
    m_targets[value].append(setting);
    setting->setParent(this);
}

QList<StandardSetting *> *StandardSetting::getSubSettings()
{
    if (m_targets.contains(m_settingValue) &&
        m_targets[m_settingValue].size() > 0)
        return &m_targets[m_settingValue];
    return &m_children;
}

bool StandardSetting::haveSubSettings()
{
    QList<StandardSetting *> *subSettings = getSubSettings();
    return subSettings && subSettings->size() > 0;
}

void StandardSetting::clearSettings()
{
    m_children.clear();
}

void StandardSetting::setValue(const QString &newValue)
{
    m_settingValue = newValue;
    m_haveChanged = true;
    emit valueChanged(newValue);
    emit valueChanged(this);
    emit ShouldRedraw(this);
}


/**
 * Return true if the setting have changed or any of its children
 */
bool StandardSetting::haveChanged()
{
    if (m_haveChanged)
    {
        return true;
    }

    //we check only the relevant children
    QList<StandardSetting *> *children = getSubSettings();
    if (!children)
        return false;

    QList<StandardSetting *>::const_iterator i;
    bool haveChanged = false;
    for (i = children->constBegin(); !haveChanged && i != children->constEnd();
         ++i)
        haveChanged = (*i)->haveChanged();

    return haveChanged;
}

void StandardSetting::Load(void)
{
    m_haveChanged = false;

    QList<StandardSetting *>::const_iterator i;
    for (i = m_children.constBegin(); i != m_children.constEnd(); ++i)
        (*i)->Load();

    QMap<QString, QList<StandardSetting *> >::const_iterator iMap;
    for (iMap = m_targets.constBegin(); iMap != m_targets.constEnd(); ++iMap)
    {
        for (i = (*iMap).constBegin(); i != (*iMap).constEnd(); ++i)
            (*i)->Load();
    }
}

void StandardSetting::Save(void)
{
    m_haveChanged = false;

    //we save only the relevant children
    QList<StandardSetting *> *children = getSubSettings();
    if (!children)
        return;

    QList<StandardSetting *>::const_iterator i;
    for (i = children->constBegin(); i != children->constEnd(); ++i)
        (*i)->Save();
}

void StandardSetting::setName(const QString &name)
{
    m_name = name;
    if (m_label.isEmpty())
        setLabel(name);
}

StandardSetting* StandardSetting::byName(const QString &name)
{
    return (name == m_name) ? this : NULL;
}

/******************************************************************************
                            Group Setting
*******************************************************************************/
GroupSetting::GroupSetting():
    StandardSetting(this), TransientStorage()
{
}

void GroupSetting::edit(MythScreenType *screen)
{
    if (!isEnabled())
        return;

    DialogCompletionEvent *dce =
        new DialogCompletionEvent("leveldown", 0, "", "");
    QCoreApplication::postEvent(screen, dce);
}

void GroupSetting::updateButton(MythUIButtonListItem *item)
{
    item->DisplayState("group", "widgettype");
    item->setEnabled(isEnabled());
    item->SetText(m_label);
    item->SetText(m_settingValue, "value");
    item->SetText(getHelpText(), "description");
    item->setDrawArrow(haveSubSettings());
}

StandardSetting* GroupSetting::byName(const QString &name)
{
    foreach (StandardSetting *setting, *getSubSettings())
    {
        StandardSetting *s = setting->byName(name);
        if (s)
            return s;
    }
    return NULL;
}

ButtonStandardSetting::ButtonStandardSetting(const QString &label):
    StandardSetting(this), TransientStorage()
{
    setLabel(label);
}

void ButtonStandardSetting::edit(MythScreenType *screen)
{
    emit clicked();
}


/******************************************************************************
                            Text Setting
*******************************************************************************/

MythUITextEditSetting::MythUITextEditSetting(Storage *_storage):
    StandardSetting(_storage)
{
}

void MythUITextEditSetting::edit(MythScreenType * screen)
{
    if (!isEnabled())
        return;

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythTextInputDialog *settingdialog =
        new MythTextInputDialog(popupStack,
                                getLabel(), FilterNone, false, m_settingValue);

    if (settingdialog->Create())
    {
        settingdialog->SetReturnEvent(screen, "editsetting");
        popupStack->AddScreen(settingdialog);
    }
    else
        delete settingdialog;
}

void MythUITextEditSetting::resultEdit(DialogCompletionEvent *dce)
{
    if (m_settingValue != dce->GetResultText())
        setValue(dce->GetResultText());
}


void MythUITextEditSetting::updateButton(MythUIButtonListItem *item)
{
    StandardSetting::updateButton(item);
    item->DisplayState("textedit", "widgettype");
}


/******************************************************************************
                            Directory Setting
*******************************************************************************/

MythUIFileBrowserSetting::MythUIFileBrowserSetting(Storage *_storage):
    StandardSetting(_storage)
{
    m_typeFilter = (QDir::AllDirs | QDir::Drives | QDir::Files |
                    QDir::Readable | QDir::Writable | QDir::Executable);
    m_nameFilter.clear();
    m_nameFilter << "*";
}

void MythUIFileBrowserSetting::edit(MythScreenType * screen)
{
    if (!isEnabled())
        return;

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythUIFileBrowser *settingdialog = new MythUIFileBrowser(popupStack,
                                                             m_settingValue);
    settingdialog->SetTypeFilter(m_typeFilter);
    settingdialog->SetNameFilter(m_nameFilter);

    if (settingdialog->Create())
    {
        settingdialog->SetReturnEvent(screen, "editsetting");
        popupStack->AddScreen(settingdialog);
    }
    else
        delete settingdialog;
}

void MythUIFileBrowserSetting::resultEdit(DialogCompletionEvent *dce)
{
    if (m_settingValue != dce->GetResultText())
        setValue(dce->GetResultText());
}

void MythUIFileBrowserSetting::updateButton(MythUIButtonListItem *item)
{
    StandardSetting::updateButton(item);
    item->DisplayState("filebrowser", "widgettype");
}


/******************************************************************************
                            ComboBoxSetting
*******************************************************************************/
/**
 * Create a Setting Widget to select the value from a list
 * \param rw if set to true, the user can input it's own value
 */
MythUIComboBoxSetting::MythUIComboBoxSetting(Storage *_storage, bool rw):
    StandardSetting(_storage),
    m_rewrite(rw),
    m_isSet(false)
{
}

MythUIComboBoxSetting::~MythUIComboBoxSetting()
{
    m_labels.clear();
    m_values.clear();
}

void MythUIComboBoxSetting::setValue(int value)
{
    if (value >= 0 && value < m_values.size())
        StandardSetting::setValue(m_values.at(value));
}

int MythUIComboBoxSetting::getValueIndex(const QString &value) const
{
    return m_values.indexOf(value);
}

void MythUIComboBoxSetting::addSelection(const QString &label, QString value,
                                         bool select)
{
    value = value.isEmpty() ? label : value;
    m_labels.push_back(label);
    m_values.push_back(value);

    if (select || !m_isSet)
    {
        StandardSetting::setValue(value);
        if (!m_isSet)
            m_isSet = true;
    }
}

void MythUIComboBoxSetting::clearSelections()
{
    m_labels.clear();
    m_values.clear();
}

void MythUIComboBoxSetting::updateButton(MythUIButtonListItem *item)
{
    item->DisplayState("combobox", "widgettype");
    item->setEnabled(isEnabled());
    item->SetText(m_label);
    int indexValue = m_values.indexOf(m_settingValue);
    if (indexValue >= 0)
        item->SetText(m_labels.value(indexValue), "value");
    else
        item->SetText(m_settingValue, "value");
    item->SetText(getHelpText(), "description");
    item->setDrawArrow(haveSubSettings());
}

void MythUIComboBoxSetting::edit(MythScreenType * screen)
{
    if (!isEnabled())
        return;

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythDialogBox *menuPopup =
        new MythDialogBox(getLabel(), popupStack, "optionmenu");

    if (menuPopup->Create())
    {
        popupStack->AddScreen(menuPopup);

        //connect(menuPopup, SIGNAL(haveResult(QString)),
        //SLOT(setValue(QString)));

        menuPopup->SetReturnEvent(screen, "editsetting");

        if (m_rewrite)
            menuPopup->AddButton(QObject::tr("New entry"),
                                 QString("NEWENTRY"),
                                 false,
                                 m_settingValue == "");
        for (int i = 0; i < m_labels.size() && m_values.size(); ++i)
        {
            QString value = m_values.at(i);
            menuPopup->AddButton(m_labels.at(i),
                                 value,
                                 false,
                                 value == m_settingValue);
        }
    }
    else
        delete menuPopup;
}

void MythUIComboBoxSetting::setValue(const QString& newValue)
{
    StandardSetting::setValue(newValue);
}

void MythUIComboBoxSetting::resultEdit(DialogCompletionEvent *dce)
{
    if (dce->GetResult() != -1)
    {
        if (m_rewrite && dce->GetData().toString() == "NEWENTRY")
        {
            MythScreenStack *popupStack =
                GetMythMainWindow()->GetStack("popup stack");

            MythTextInputDialog *settingdialog =
                new MythTextInputDialog(popupStack, getLabel(), FilterNone,
                                        false, m_settingValue);

            if (settingdialog->Create())
            {
                connect(settingdialog, SIGNAL(haveResult(QString)),
                        SLOT(setValue(const QString&)));
                popupStack->AddScreen(settingdialog);
            }
            else
                delete settingdialog;
        }
        else if (m_settingValue != dce->GetData().toString())
            StandardSetting::setValue(dce->GetData().toString());
    }
}

void MythUIComboBoxSetting::Load()
{
    StandardSetting::Load();
}

/******************************************************************************
                            SpinBox Setting
*******************************************************************************/
MythUISpinBoxSetting::MythUISpinBoxSetting(Storage *_storage, int min, int max,
                                           int step, bool allow_single_step,
                                           const QString &special_value_text)
    : MythUIComboBoxSetting(_storage, false),
      m_min(min),
      m_max(max),
      m_step(step),
      m_allow_single_step(allow_single_step),
      m_special_value_text(special_value_text)
{
    //we default to 0 unless 0 is out of range
    if (m_min > 0 || m_max < 0)
        m_settingValue = QString::number(m_min);
}

void MythUISpinBoxSetting::updateButton(MythUIButtonListItem *item)
{
    item->DisplayState("spinbox", "widgettype");
    item->setEnabled(isEnabled());
    item->SetText(m_label);
    int indexValue = m_values.indexOf(m_settingValue);
    if (indexValue >= 0)
        item->SetText(m_labels.value(indexValue), "value");
    else
    {
        if (m_settingValue.toInt() == m_min && !m_special_value_text.isEmpty())
            item->SetText(m_special_value_text, "value");
        else
            item->SetText(m_settingValue, "value");
    }
    item->SetText(getHelpText(), "description");
    item->setDrawArrow(haveSubSettings());
}

int MythUISpinBoxSetting::intValue()
{
    return m_settingValue.toInt();
}

void MythUISpinBoxSetting::edit(MythScreenType * screen)
{
    if (!isEnabled())
        return;

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythSpinBoxDialog *settingdialog = new MythSpinBoxDialog(popupStack,
                                                             getLabel());

    if (settingdialog->Create())
    {
        settingdialog->SetRange(m_min, m_max, m_step);
        //Add custom values
        for (int i = 0; i < m_labels.size() && m_values.size(); ++i)
        {
            QString value = m_values.at(i);
            settingdialog->AddSelection(m_labels.at(i), value.toInt());
        }
        if (!m_special_value_text.isEmpty())
            settingdialog->AddSelection(m_special_value_text, m_min);
        settingdialog->SetValue(m_settingValue);
        settingdialog->SetReturnEvent(screen, "editsetting");
        popupStack->AddScreen(settingdialog);
    }
    else
        delete settingdialog;
}

void MythUISpinBoxSetting::resultEdit(DialogCompletionEvent *dce)
{
    if (m_settingValue != dce->GetResultText())
        MythUIComboBoxSetting::setValue(dce->GetResultText());
}

/******************************************************************************
                           MythUICheckBoxSetting
*******************************************************************************/

MythUICheckBoxSetting::MythUICheckBoxSetting(Storage *_storage):
    StandardSetting(_storage)
{
}

bool MythUICheckBoxSetting::boolValue()
{
    return m_settingValue == "1";
}

void MythUICheckBoxSetting::setValue(const QString &value)
{
    StandardSetting::setValue(value);
    emit valueChanged(value == "1");
}

void MythUICheckBoxSetting::setValue(bool value)
{
    StandardSetting::setValue(value ? "1" : "0");
    emit valueChanged(value);
}

void MythUICheckBoxSetting::updateButton(MythUIButtonListItem *item)
{
    StandardSetting::updateButton(item);
    item->DisplayState("checkbox", "widgettype");
    item->setCheckable(true);
    item->SetText("", "value");
    if (m_settingValue == "1")
        item->setChecked(MythUIButtonListItem::FullChecked);
    else
        item->setChecked(MythUIButtonListItem::NotChecked);
}

void MythUICheckBoxSetting::edit(MythScreenType * screen)
{
    if (!isEnabled())
        return;

    DialogCompletionEvent *dce =
        new DialogCompletionEvent("editsetting", 0, "", "");
    QCoreApplication::postEvent(screen, dce);
}

void MythUICheckBoxSetting::resultEdit(DialogCompletionEvent *dce)
{
    setValue(!boolValue());
}

/******************************************************************************
                           Standard setting dialog
*******************************************************************************/

StandardSettingDialog::StandardSettingDialog(MythScreenStack *parent,
                                             const char *name,
                                             GroupSetting *groupSettings) :
    MythScreenType(parent, name),
    m_buttonList(0),
    m_title(0),
    m_groupHelp(0),
    m_selectedSettingHelp(0),
    m_menuPopup(0),
    m_settingsTree(groupSettings),
    m_currentGroupSetting(0)
{
}

StandardSettingDialog::~StandardSettingDialog()
{
    if (m_settingsTree)
        m_settingsTree->deleteLater();
}

bool StandardSettingDialog::Create(void)
{
    if (!LoadWindowFromXML("standardsetting-ui.xml", "settingssetup", this))
        return false;

    bool error = false;
    UIUtilE::Assign(this, m_title, "title", &error);
    UIUtilW::Assign(this, m_groupHelp, "grouphelp", &error);
    UIUtilE::Assign(this, m_buttonList, "settingslist", &error);

    UIUtilW::Assign(this, m_selectedSettingHelp, "selectedsettinghelp");

    if (error)
    {
        LOG(VB_GENERAL, LOG_ERR, "Theme elements missing.");
        return false;
    }

    connect(m_buttonList, SIGNAL(itemSelected(MythUIButtonListItem*)),
            SLOT(settingSelected(MythUIButtonListItem*)));
    connect(m_buttonList, SIGNAL(itemClicked(MythUIButtonListItem*)),
            SLOT(settingClicked(MythUIButtonListItem*)));

    BuildFocusList();

    LoadInBackground();

    return true;
}

void StandardSettingDialog::settingSelected(MythUIButtonListItem *item)
{
    if (!item)
        return;

    StandardSetting *setting = qVariantValue<StandardSetting*>(item->GetData());
    if (setting && m_selectedSettingHelp)
    {
        m_selectedSettingHelp->SetText(setting->getHelpText());
        if (m_selectedSettingHelp->GetText().isEmpty())
            m_selectedSettingHelp->SetText("This setting need a description");
    }
}

void StandardSettingDialog::settingClicked(MythUIButtonListItem *item)
{
    StandardSetting* setting = item->GetData().value<StandardSetting*>();
    if (setting)
        setting->edit(this);
}

void StandardSettingDialog::customEvent(QEvent *event)
{
    if (event->type() == DialogCompletionEvent::kEventType)
    {
        DialogCompletionEvent *dce = (DialogCompletionEvent*)(event);
        QString resultid  = dce->GetId();

        if (resultid == "leveldown")
        {
            //a GroupSetting have been clicked
            LevelDown();
        }
        else if (resultid == "editsetting")
        {
            MythUIButtonListItem * item = m_buttonList->GetItemCurrent();
            if (item)
            {
                StandardSetting *ss = item->GetData().value<StandardSetting*>();
                if (ss)
                    ss->resultEdit(dce);
            }
        }
        else if (resultid == "exit")
        {
            int buttonnum = dce->GetResult();
            if (buttonnum == 0)
            {
                Save();
                MythScreenType::Close();
                if (m_settingsTree)
                    m_settingsTree->applyChange();
            }
            else if (buttonnum == 1)
                MythScreenType::Close();
        }
    }
}

void StandardSettingDialog::Load(void)
{
    if (m_settingsTree)
        m_settingsTree->Load();
}

void StandardSettingDialog::Init(void)
{
    setCurrentGroupSetting(m_settingsTree);
}

GroupSetting *StandardSettingDialog::GetGroupSettings(void) const
{
    return m_settingsTree;
}

void StandardSettingDialog::setCurrentGroupSetting(
    StandardSetting *groupSettings, StandardSetting *selectedSetting)
{
    if (!groupSettings)
        return;

    if (m_currentGroupSetting)
    {
        disconnect(m_currentGroupSetting,
                   SIGNAL(settingsChanged(StandardSetting *)), 0, 0);
        m_currentGroupSetting->Close();
    }

    m_currentGroupSetting = groupSettings;
    m_currentGroupSetting->Open();

    m_title->SetText(m_currentGroupSetting->getLabel());
    if (m_title->GetText().isEmpty())
        m_title->SetText("This group need a title");
    if (m_groupHelp)
    {
        m_groupHelp->SetText(m_currentGroupSetting->getHelpText());
        if (m_groupHelp->GetText().isEmpty())
            m_groupHelp->SetText("This group need a description");
    }
    updateSettings(selectedSetting);
    connect(m_currentGroupSetting,
            SIGNAL(settingsChanged(StandardSetting *)),
            SLOT(updateSettings(StandardSetting *)));
}

void StandardSettingDialog::updateSettings(StandardSetting * selectedSetting)
{
    m_buttonList->Reset();
    if (!m_currentGroupSetting->haveSubSettings())
        return;

    QList<StandardSetting *> *settings =
        m_currentGroupSetting->getSubSettings();
    if (!settings)
        return;

    QList<StandardSetting *>::const_iterator i;
    MythUIButtonListItem *selectedItem = NULL;
    for (i = settings->constBegin(); i != settings->constEnd(); ++i)
    {
        if (selectedSetting == (*i))
            selectedItem = (*i)->createButton(m_buttonList);
        else
            (*i)->createButton(m_buttonList);
    }
    if (selectedItem)
        m_buttonList->SetItemCurrent(selectedItem);
    settingSelected(m_buttonList->GetItemCurrent());
}

void StandardSettingDialog::Save()
{
    if (m_settingsTree)
        m_settingsTree->Save();
}

void StandardSettingDialog::LevelUp()
{
    if (!m_currentGroupSetting)
        return;

    if (m_currentGroupSetting->getParent())
    {
        setCurrentGroupSetting(m_currentGroupSetting->getParent(),
                               m_currentGroupSetting);
    }
}

void StandardSettingDialog::LevelDown()
{
    MythUIButtonListItem *item = m_buttonList->GetItemCurrent();
    if (item)
    {
        StandardSetting *ss = item->GetData().value<StandardSetting*>();
        if (ss && ss->haveSubSettings() && ss->isEnabled())
            setCurrentGroupSetting(ss);
    }
}

void StandardSettingDialog::Close(void)
{
    if (m_settingsTree->haveChanged())
    {
        QString label = tr("Exit ?");

        MythScreenStack *popupStack =
            GetMythMainWindow()->GetStack("popup stack");

        MythDialogBox * menuPopup =
            new MythDialogBox(label, popupStack, "exitmenu");

        if (menuPopup->Create())
        {
            popupStack->AddScreen(menuPopup);

            menuPopup->SetReturnEvent(this, "exit");

            menuPopup->AddButton(tr("Save then Exit"));
            menuPopup->AddButton(tr("Exit without saving changes"));
            menuPopup->AddButton(tr("Cancel"));
        }
        else
            delete menuPopup;
    }
    else
        MythScreenType::Close();
}


bool StandardSettingDialog::keyPressEvent(QKeyEvent *e)
{
    QStringList actions;
    bool handled = m_buttonList->keyPressEvent(e);
    if (handled)
        return true;

    handled = GetMythMainWindow()->TranslateKeyPress("Global", e, actions);

    //send the key to the selected Item first
    MythUIButtonListItem * item = m_buttonList->GetItemCurrent();
    if (item)
    {
        StandardSetting *ss = item->GetData().value<StandardSetting*>();
        if (ss)
            handled = ss->keyPressEvent(e);
    }
    if (handled)
        return true;

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "LEFT")
        {
            if (m_currentGroupSetting &&
                m_currentGroupSetting == m_settingsTree)
                Close();
            else
                LevelUp();
        }
        else if (action == "RIGHT")
            LevelDown();
        else
            handled = MythScreenType::keyPressEvent(e);
    }

    return handled;
}

