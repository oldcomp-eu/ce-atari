#include <stdio.h>
#include <string.h>

#include "../global.h"
#include "../native/scsi_defs.h"
#include "../acsidatatrans.h"

#include "../settings.h"
#include "keys.h"
#include "configstream.h"
#include "../debug.h"

ConfigStream::ConfigStream()
{
    stScreenWidth   = 40;
    gotoOffset      = 0;

    enterKeyEventLater = 0;
    
    showingHomeScreen	= false;
    showingMessage		= false;
    screenChanged		= true;

    dataTrans   = NULL;
    reloadProxy = NULL;

    message.clear();
    createScreen_homeScreen();
}

ConfigStream::~ConfigStream()
{
    destroyCurrentScreen();
    destroyScreen(message);
}

void ConfigStream::setAcsiDataTrans(AcsiDataTrans *dt)
{
    dataTrans = dt;
}

void ConfigStream::setSettingsReloadProxy(SettingsReloadProxy *rp)
{
    reloadProxy = rp;
}

void ConfigStream::processCommand(BYTE *cmd, int writeToFd)
{
    static BYTE readBuffer[READ_BUFFER_SIZE];
    int streamCount;

    if(cmd[1] != 'C' || cmd[2] != 'E' || cmd[3] != HOSTMOD_CONFIG) {        // not for us?
        return;
    }

    dataTrans->clear();                 // clean data transporter before handling

    switch(cmd[4]) {
    case CFG_CMD_IDENTIFY:          // identify?
        dataTrans->addDataBfr((BYTE *) "CosmosEx config console", 23, true);       // add identity string with padding
        dataTrans->setStatus(SCSI_ST_OK);
        break;

    case CFG_CMD_KEYDOWN:
        onKeyDown(cmd[5]);                                                // first send the key down signal
        
        if(enterKeyEventLater) {                                            // if we should handle some event 
            enterKeyHandler(enterKeyEventLater);                            // handle it
            enterKeyEventLater = 0;                                         // and don't let it handle next time
        }

        streamCount = getStream(false, readBuffer, READ_BUFFER_SIZE);     // then get current screen stream

        dataTrans->addDataBfr(readBuffer, streamCount, true);                              // add data and status, with padding to multiple of 16 bytes
        dataTrans->setStatus(SCSI_ST_OK);

        Debug::out(LOG_DEBUG, "handleConfigStream -- CFG_CMD_KEYDOWN -- %d bytes", streamCount);
        break;

    case CFG_CMD_SET_RESOLUTION:
        switch(cmd[5]) {
        case ST_RESOLUTION_LOW:     stScreenWidth = 40; break;
        case ST_RESOLUTION_MID:
        case ST_RESOLUTION_HIGH:    stScreenWidth = 80; break;
        }

        gotoOffset = (stScreenWidth - 40) / 2;

        destroyCurrentScreen();                     // the resolution might have changed, so destroy and screate the home screen again
        createScreen_homeScreen();

        dataTrans->setStatus(SCSI_ST_OK);
        Debug::out(LOG_DEBUG, "handleConfigStream -- CFG_CMD_SET_RESOLUTION -- %d", cmd[5]);
        break;

    case CFG_CMD_REFRESH:
        screenChanged = true;                                           // get full stream, not only differences
        streamCount = getStream(false, readBuffer, READ_BUFFER_SIZE);   // then get current screen stream

        dataTrans->addDataBfr(readBuffer, streamCount, true);              // add data and status, with padding to multiple of 16 bytes
        dataTrans->setStatus(SCSI_ST_OK);

        Debug::out(LOG_DEBUG, "handleConfigStream -- CFG_CMD_REFRESH -- %d bytes", streamCount);
        break;

    case CFG_CMD_GO_HOME:
        streamCount = getStream(true, readBuffer, READ_BUFFER_SIZE);      // get homescreen stream

        dataTrans->addDataBfr(readBuffer, streamCount, true);                              // add data and status, with padding to multiple of 16 bytes
        dataTrans->setStatus(SCSI_ST_OK);

        Debug::out(LOG_DEBUG, "handleConfigStream -- CFG_CMD_GO_HOME -- %d bytes", streamCount);
        break;
        
    case CFG_CMD_LINUXCONSOLE_GETSTREAM:                                // get the current bash console stream
        if(cmd[5] != 0) {                                               // if it's a real key, send it
            linuxConsole_KeyDown(cmd[5]);
        }
            
        streamCount = linuxConsole_getStream(readBuffer, 3 * 512);      // get the stream from shell
        dataTrans->addDataBfr(readBuffer, streamCount, true);           // add data and status, with padding to multiple of 16 bytes
        dataTrans->setStatus(SCSI_ST_OK);
        
        break;
        
    default:                            // other cases: error
        dataTrans->setStatus(SCSI_ST_CHECK_CONDITION);
        break;
    }

    if(writeToFd == -1) {
        dataTrans->sendDataAndStatus();     // send all the stuff after handling, if we got any
    } else {
        dataTrans->sendDataToFd(writeToFd);
    }
}

void ConfigStream::onKeyDown(BYTE key)
{
    StupidVector &scr = showingMessage ? message : screen;		// if we should show message, set reference to message, otherwise set reference to screen

    int focused = -1, firstFocusable = -1, lastFocusable = -1;

    // go through the current screen and find focused component, also first focusable component
    for(WORD i=0; i<scr.size(); i++) {
        ConfigComponent *c = (ConfigComponent *) scr[i];

        if(c->isFocused()) {						// if found focused component, store index
            focused = i;
        }

        if(firstFocusable == -1) {					// if found focusable component, store index
            if(c->canFocus()) {
                firstFocusable = i;
            }
        }

        if(c->canFocus()) {							// if this is focusable, then store it as last focusable (at the end it will contain the last focusable)
            lastFocusable = i;
        }
    }

    if(firstFocusable == -1) {						// nothing focusable? do nothing
        return;
    }

    if(focused == -1) {								// there is something focusable, but nothing has focus? focus it
        focused = firstFocusable;
    }

    ConfigComponent *curr = (ConfigComponent *) scr[focused];		// focus this component
    curr->setFocus(true);

    int prevFocusable = -1, nextFocusable = -1;		// now find previous and next focusable item in the list of components
    for(WORD i=0; i<scr.size(); i++) {
        ConfigComponent *c = (ConfigComponent *) scr[i];

        if(!c->canFocus()) {						// can't focus? fuck you!
            continue;
        }

        if(i < focused) {							// if we're bellow currently focused item, store each found index (go near focused component)
            prevFocusable = i;
        }

        if(i > focused) {							// if we're above currently focused item, store only first found index (don't go far from focused component)
            if(nextFocusable == -1) {
                nextFocusable = i;
            }
        }
    }

    if(curr->isGroupCheckBox()) {                           // for group check boxes
        int groupid, chbid;
        curr->getCheckboxGroupIds(groupid, chbid);          // ge the checkbox group IDs

        if(focusNextCheckboxGroup(key, groupid, chbid)) {   // and try to move there
            curr->setFocus(false);                          // and unfocus the previous one
            return;
        }
    }

    if(key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) {     // in case of left, right, tab on SAVE and CANCEL
        if(curr->getComponentType() == ConfigComponent::button) {
            if(key == KEY_LEFT) {
                key = KEY_UP;
            }
            
            if(key == KEY_RIGHT || key == KEY_TAB) {
                key = KEY_DOWN;
            }
        }
    }
    
    if(key == KEY_UP) {							// arrow up
        curr->setFocus(false);					// unfocus this component

        if(prevFocusable != -1) {				                    // got previous focusable item?
            curr = (ConfigComponent *) scr[prevFocusable];          // move to the previous component
        } else if(lastFocusable != -1) {		                    // got last focusable?
            curr = (ConfigComponent *) scr[lastFocusable];          // move to the last component (wrap around)
        }

        curr->setFocus(true);					// focus this component

        return;
    }

    if(key == KEY_DOWN) {							// arrow down
        curr->setFocus(false);					// unfocus this component

        if(nextFocusable != -1) {				                // got next focusable item?
            curr = (ConfigComponent *) scr[nextFocusable];		// move to the next component
        } else if(firstFocusable != -1) {		                // got first focusable?
            curr = (ConfigComponent *) scr[firstFocusable];		// move to the first component (wrap around)
        }

        curr->setFocus(true);					// focus this component

        return;
    }

    if(key == KEY_ESC) {                        // esc as cancel
        enterKeyHandlerLater(CS_GO_HOME);
        return;
    }

    // if it got here, we didn't handle it, let the component handle it
    curr->onKeyPressed(key);
}

int ConfigStream::getStream(bool homeScreen, BYTE *bfr, int maxLen)
{
    int totalCnt = 0;
    
    if(showingMessage) {								// if we're showing the message
        if(homeScreen) {								// but we should show home screen
            hideMessageScreen();						// hide the message
        }
    }

    if(homeScreen) {									// if we should show the stream for homescreen
        if(!showingHomeScreen) {						// and we're not showing it yet
            createScreen_homeScreen();					// create homescreen
        }

        screenChanged = true;                                           // mark that the screen has changed
    }

    if(screen.size() == 0) {							// if we wanted to show current screen, but there is nothing, just show home screen
        createScreen_homeScreen();
    }

    memset(bfr, 0, maxLen);								// clear the buffer

    StupidVector &scr = showingMessage ? message : screen;		// if we should show message, set reference to message, otherwise set reference to screen

    // first turn off the cursor to avoid cursor blinking on the screen
    *bfr++ = 27;
    *bfr++ = 'f';       // CUR_OFF
    totalCnt += 2;

    if(screenChanged) {									// if screen changed, clear screen (CLEAR_HOME) and draw it all
        *bfr++ = 27;
        *bfr++ = 'E';   // CLEAR_HOME
        totalCnt += 2;
    }

    //--------
    int focused = -1;

    for(WORD i=0; i<scr.size(); i++) {				// go through all the components of screen and gather their streams
        ConfigComponent *c = (ConfigComponent *) scr[i];

        if(c->isFocused()) {							// if this component has focus, store it's index
            focused = i;
        }

        int gotLen;
        c->getStream(screenChanged, bfr, gotLen);		// if screenChanged, will get full stream, not only change
        bfr += gotLen;

        totalCnt += gotLen;
    }

    if(focused != -1) {									// if got some component with focus
        int gotLen;
        ConfigComponent *c = (ConfigComponent *) scr[focused];
        c->terminal_addGotoCurrentCursor(bfr, gotLen);	// position the cursor at the right place

        bfr         += gotLen;
        totalCnt    += gotLen;
    }

    screenChanged = false;
	
	*bfr++ = 0;									        // add string terminator
	totalCnt++;

    *bfr++ = isUpdateScreen();                   // after the string store a flag if this screen is update screen, so the ST client could show good message on update
	totalCnt++;
    
    return totalCnt;                                    // return the count of bytes used
}

void ConfigStream::showMessageScreen(char *msgTitle, char *msgTxt)
{
    screenChanged = true;

    showingMessage = true;
    destroyScreen(message);

    screen_addHeaderAndFooter(message, msgTitle);

    ConfigComponent *comp;

    int labelX = 0, labelY = 10;

    std::string msgTxt2 = msgTxt;
    replaceNewLineWithGoto(msgTxt2, labelX + gotoOffset, labelY);           // this will replace all \n with VT52 goto command

    comp = new ConfigComponent(this, ConfigComponent::label, msgTxt2.c_str(), 240, labelX, labelY, gotoOffset);
    message.push_back(comp);

    comp = new ConfigComponent(this, ConfigComponent::button, " OK ", 4, 17, 20, gotoOffset);
    comp->setOnEnterFunctionCode(CS_HIDE_MSG_SCREEN);
    comp->setFocus(true);
    message.push_back(comp);
}

void ConfigStream::hideMessageScreen(void)
{
    screenChanged = true;

    showingMessage = false;
    destroyScreen(message);
}

void ConfigStream::destroyCurrentScreen(void)
{
    destroyScreen(screen);
}

void ConfigStream::destroyScreen(StupidVector &scr)
{
    for(WORD i=0; i<scr.size(); i++) {				// go through this screen, delete all components
        ConfigComponent *c = (ConfigComponent *) scr[i];
        delete c;
    }

    scr.clear();									// now clear the list
}

void ConfigStream::setFocusToFirstFocusable(void)
{
    for(WORD i=0; i<screen.size(); i++) {			// go through the current screen
        ConfigComponent *c = (ConfigComponent *) screen[i];

        if(c->canFocus()) {
            c->setFocus(true);
            return;
        }
    }
}

ConfigComponent *ConfigStream::findComponentById(int compId)
{
    for(WORD i=0; i<screen.size(); i++) {			// go through the current screen
        ConfigComponent *c = (ConfigComponent *) screen[i];

        if(c->getComponentId() == compId) {                     // found the component?
            return c;
        }
    }

    return NULL;
}

bool ConfigStream::getTextByComponentId(int componentId, std::string &text)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return false;
    }

    c->getText(text);
    return true;
}

void ConfigStream::setTextByComponentId(int componentId, std::string &text)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return;
    }

    c->setText(text);
}

void ConfigStream::setIntByComponentId(int componentId, int value)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return;
    }

    char tmp[32];
    sprintf(tmp, "%d", value);
    
    std::string text = tmp;
    c->setText(text);
}

bool ConfigStream::getIntByComponentId(int componentId, int &value)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return false;
    }

    std::string text;
    c->getText(text);
    
    int ires = sscanf((char *) text.c_str(), "%d", &value);
    
    if(ires != 1) {
        return false;
    }
    
    return true;
}

void ConfigStream::setFloatByComponentId(int componentId, float value)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return;
    }

    char tmp[32];
    sprintf(tmp, "%f", value);
    
    std::string text = tmp;
    c->setText(text);
}

bool ConfigStream::getFloatByComponentId(int componentId, float &value)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return false;
    }

    std::string text;
    c->getText(text);
    
    int ires = sscanf((char *) text.c_str(), "%f", &value);
    
    if(ires != 1) {
        return false;
    }
    
    return true;
}

bool ConfigStream::getBoolByComponentId(int componentId, bool &val)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return false;
    }

    val = c->isChecked();

    return true;
}

void ConfigStream::setBoolByComponentId(int componentId, bool &val)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return;
    }

    c->setIsChecked(val);
}

void ConfigStream::focusByComponentId(int componentId)
{
    ConfigComponent *c = findComponentById(componentId);

    if(c == NULL) {
        return;
    }

    c->setFocus(true);
}

bool ConfigStream::focusNextCheckboxGroup(BYTE key, int groupid, int chbid)
{
    for(WORD i=0; i<screen.size(); i++) {			// go through the current screen
        ConfigComponent *c = (ConfigComponent *) screen[i];

        if(c->isGroupCheckBox()) {
            int groupid2, chbid2;

            c->getCheckboxGroupIds(groupid2, chbid2);

            // on key UP find groupid which is smaller by 1
            if(key == KEY_UP && groupid == (groupid2 + 1) && chbid == chbid2) {
                c->setFocus(true);
                return true;
            }

            // on key DOWN find groupid which is greater by 1
            if(key == KEY_DOWN && groupid == (groupid2 - 1) && chbid == chbid2) {
                c->setFocus(true);
                return true;
            }

            // on key LEFT just find smaller chbid
            if(key == KEY_LEFT && groupid == groupid2 && chbid == (chbid2 + 1)) {
                c->setFocus(true);
                return true;
            }

            // on key RIGHT just find greater chbid
            if(key == KEY_RIGHT && groupid == groupid2 && chbid == (chbid2 - 1)) {
                c->setFocus(true);
                return true;
            }
        }
    }

    return false;
}

int ConfigStream::checkboxGroup_getCheckedId(int groupId) 
{
    for(WORD i=0; i<screen.size(); i++) {					// go through the current screen and find the checked checkbox
        ConfigComponent *c = (ConfigComponent *) screen[i];

        int thisGroupId, checkboxId;
        c->getCheckboxGroupIds(thisGroupId, checkboxId);	// get the IDs

        if(thisGroupId != groupId) {        				// if the group ID doesn't match, skip
            continue;
        }

        if(c->isChecked()) {								// is checked and from the group?
            return checkboxId;
        }
    }

    return -1;
}

void ConfigStream::checkboxGroup_setCheckedId(int groupId, int checkedId)
{
    for(WORD i=0; i<screen.size(); i++) {					// go through the current screen and find the checked checkbox
        ConfigComponent *c = (ConfigComponent *) screen[i];

        int thisGroupId, checkboxId;
        c->getCheckboxGroupIds(thisGroupId, checkboxId);	// get the IDs

        if(thisGroupId != groupId) {        				// if the group ID doesn't match, skip
            continue;
        }

        if(checkboxId == checkedId) {						// for the matching id - check
            c->setIsChecked(true);
        } else {											// for mismatching id - uncheck
            c->setIsChecked(false);
        }
    }
}

void ConfigStream::onCheckboxGroupEnter(int groupId, int checkboxId)
{
    checkboxGroup_setCheckedId(groupId, checkboxId);
}

void ConfigStream::screen_addHeaderAndFooter(StupidVector &scr, char *screenName)
{
    ConfigComponent *comp;

    // insert header
    comp = new ConfigComponent(this, ConfigComponent::label, ">> CosmosEx config tool - 2013 - 2015 <<", 40, 0, 0, gotoOffset);
    comp->setReverse(true);
    scr.push_back(comp);

    // insert footer
    comp = new ConfigComponent(this, ConfigComponent::label, " F5 - refresh, F8 - console, F10 - quit ", 40, 0, 24, gotoOffset);
    comp->setReverse(true);
    scr.push_back(comp);

    // insert screen name as part of header
    char bfr[41];
    memset(bfr, 32, 40);					// fill with spaces (0 - 39)
    bfr[40] = 0;							// terminate with a zero

    int len = strlen(screenName);
    int pos = (40 / 2) - (len / 2);			// calculate the position in the middle of screen
    strncpy(bfr + pos, screenName, len);	// copy the string in the middle, withouth the terminating zero

    comp = new ConfigComponent(this, ConfigComponent::label, bfr, 40, 0, 1, gotoOffset);
    comp->setReverse(true);
    scr.push_back(comp);
}

//--------------------------
void ConfigStream::enterKeyHandlerLater(int event)
{
    enterKeyEventLater = event;
}

void ConfigStream::enterKeyHandler(int event)
{
    switch(event) {
    case CS_GO_HOME:            createScreen_homeScreen();      break;
    case CS_CREATE_ACSI:        createScreen_acsiConfig();      break;
    case CS_CREATE_TRANSLATED:  createScreen_translated();      break;
    case CS_CREATE_SHARED:      createScreen_shared();          break;
    case CS_CREATE_FLOPPY_CONF: createScreen_floppy_config();   break;
    case CS_CREATE_NETWORK:     createScreen_network();         break;
    case CS_CREATE_OTHER:       createScreen_other();           break;
    case CS_CREATE_UPDATE:      createScreen_update();          break;

    case CS_HIDE_MSG_SCREEN:    hideMessageScreen();        break;

    case CS_SAVE_ACSI:          onAcsiConfig_save();        break;
    case CS_SAVE_TRANSLATED:    onTranslated_save();        break;
    case CS_SAVE_NETWORK:       onNetwork_save();           break;

    case CS_UPDATE_CHECK:       onUpdateCheck();            break;
    case CS_UPDATE_CHECK_USB:   onUpdateCheckUsb();         break;
    case CS_UPDATE_UPDATE:      onUpdateUpdate();           break;

    case CS_OTHER_SAVE:         onOtherSave();              break;
    case CS_RESET_SETTINGS:     onResetSettings();          break;

//    case CS_SHARED_TEST:        onSharedTest();             break;
    case CS_SHARED_SAVE:        onSharedSave();             break;

    case CS_FLOPPY_CONFIG_SAVE: onFloppyConfigSave();       break;
    }
}

bool ConfigStream::verifyAndFixIPaddress(std::string &in, std::string &out, bool emptyIsOk)
{
    char ip[40];
    strcpy(ip, in.c_str());

    if(in.length() == 0) {      // empty string might be OK
        if(emptyIsOk) {
            return true;
        } else {
            return false;
        }
    }

    // try to read the numbers
    int i1, i2, i3, i4;
    bool res = sscanf(ip, "%d.%d.%d.%d", &i1, &i2, &i3, &i4);

    if(!res) {          // couldn't read the numbers?
        return false;
    }

    // numbers out of range?
    if(i1 < 0 || i1 > 255 || i2 < 0 || i2 > 255 || i3 < 0 || i3 > 255 || i4 < 0 || i4 > 255) {
        return false;
    }

    // format it back (in case there would be something extra in the field)
    sprintf(ip, "%d.%d.%d.%d", i1, i2, i3, i4);
    out = ip;

    return true;
}

void ConfigStream::replaceNewLineWithGoto(std::string &line, int startX, int startY)
{
    char gotoCmd[5];
    gotoCmd[0] = 27;
    gotoCmd[1] = 'Y';
    gotoCmd[4] = 0;

    startY++;                               // first goto will be in the next line, not in the same line this started

    // find \n and replace it with VT52 goto command
    while(1) {
        size_t pos = line.find("\n");       // find new line char

        if(pos == std::string::npos) {      // not found? quit
            break;
        }

        gotoCmd[2] = ' ' + startY;          // update the GOTO coordinates
        gotoCmd[3] = ' ' + startX;
        startY++;

        line.replace(pos, 1, gotoCmd);      // replace this \n with GOTO 
    }

    // find \r and remove it
    while(1) {
        size_t pos = line.find("\r");

        if(pos == std::string::npos) {      // not found? quit
            break;
        }

        line.replace(pos, 1, "");           // replace this \r with empty string 
    }
}

BYTE ConfigStream::isUpdateScreen(void)
{
    ConfigComponent *c1 = findComponentById(COMPID_UPDATE_COSMOSEX);    // if we have this component, then we're on update versions screen
    ConfigComponent *c2 = findComponentById(COMPID_DL1);                // if we have this component, then we're on update download screen

    if(c1 == NULL && c2 == NULL) {                                      // both update components unavailable? not update screen
        return 0;
    }

    return 1;                                                           // one of the update components is available, so we're on the update screen
}
