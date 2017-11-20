/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
/*
 * btrCore_dbus_bluez5.c
 * Implementation of DBus layer abstraction for BT functionality (BlueZ 5.37)
 */

/* System Headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>

/* External Library Headers */
#include <dbus/dbus.h>

/* Local Headers */
#include "btrCore_bt_ifce.h"
#include "btrCore_priv.h"


#define BD_NAME_LEN                         248

#define BT_DBUS_BLUEZ_PATH                  "org.bluez"
#define BT_DBUS_BLUEZ_ADAPTER_PATH          "org.bluez.Adapter1"
#define BT_DBUS_BLUEZ_DEVICE_PATH           "org.bluez.Device1"
#define BT_DBUS_BLUEZ_MEDIA_PATH            "org.bluez.Media1"
#define BT_DBUS_BLUEZ_MEDIA_ENDPOINT_PATH   "org.bluez.MediaEndpoint1"
#define BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH  "org.bluez.MediaTransport1"
#define BT_DBUS_BLUEZ_MEDIA_CTRL_PATH       "org.bluez.MediaControl1"
#define BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH     "org.bluez.MediaPlayer1"
#define BT_DBUS_BLUEZ_MEDIA_ITEM_PATH       "org.bluez.MediaItem1"
#define BT_DBUS_BLUEZ_MEDIA_FOLDER_PATH     "org.bluez.MediaFolder1"
#define BT_DBUS_BLUEZ_AGENT_PATH            "org.bluez.Agent1"
#define BT_DBUS_BLUEZ_AGENT_MGR_PATH        "org.bluez.AgentManager1"

#define BT_MEDIA_A2DP_SINK_ENDPOINT         "/MediaEndpoint/A2DPSink"
#define BT_MEDIA_A2DP_SOURCE_ENDPOINT       "/MediaEndpoint/A2DPSource"


typedef struct _stBTMediaInfo {
    unsigned char   ui8Codec;
    char            pcState[BT_MAX_STR_LEN];
    char            pcUUID[BT_MAX_STR_LEN];
    unsigned short  ui16Delay;
    unsigned short  ui16Volume;
} stBTMediaInfo;

/* Static Function Prototypes */
static int btrCore_BTHandleDusError (DBusError* aDBusErr, int aErrline, const char* aErrfunc);
static const char* btrCore_DBusType2Name (int ai32DBusMessageType);
    

static DBusHandlerResult btrCore_BTDBusConnectionFilter_cb (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTMediaEndpointHandler_cb (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentMessageHandler_cb (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);

static char* btrCore_BTGetDefaultAdapterPath (void);
static int btrCore_BTReleaseDefaultAdapterPath (void);

static DBusHandlerResult btrCore_BTAgentRelease (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentRequestPincode (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentRequestPasskey (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentRequestConfirmation(DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentAuthorize (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);
static DBusHandlerResult btrCore_BTAgentCancelMessage (DBusConnection* apDBusConn, DBusMessage* apDBusMsg, void* apvUserData);

static DBusMessage* btrCore_BTSendMethodCall (const char* objectpath, const char* interfacename, const char* methodname);

static void btrCore_BTConnectDeviceConfirmation_cb (DBusPendingCall* pending_call, void* data);

static int btrCore_BTGetDeviceInfo (stBTDeviceInfo* apstBTDeviceInfo, const char* apcIface);
static int btrCore_BTParseDevice (DBusMessage* apDBusMsg, stBTDeviceInfo* apstBTDeviceInfo);

static int btrCore_BTGetMediaInfo (stBTMediaInfo* apstBTDeviceInfo, const char* apcIface);
static int btrCore_BTParseMediaTransport (DBusMessage* apDBusMsg, stBTMediaInfo*  apstBTMediaInfo); 

static enBTDeviceType btrCore_BTMapDevClasstoDevType(unsigned int lui32Class);

static int btrCore_BTGetInterfaceProperty (void* apBtConn, const char* apBtObjectPath, const char* apBtInterfacePath, const char* mediaProperty
                                                                                                             , void* mediaPropertyValue);
#if 0
static int btrCore_BTParsePropertyChange (DBusMessage* apDBusMsg, stBTDeviceInfo* apstBTDeviceInfo);
#endif
static DBusMessage* btrCore_BTMediaEndpointSelectConfiguration (DBusMessage* apDBusMsg);
static DBusMessage* btrCore_BTMediaEndpointSetConfiguration (DBusMessage* apDBusMsg);
static DBusMessage* btrCore_BTMediaEndpointClearConfiguration (DBusMessage* apDBusMsg);


/* Static Global Variables Defs */
static char *gpcBTOutPassCode = NULL;
static int do_reject = 0;
static char gpcDeviceCurrState[BT_MAX_STR_LEN] = {'\0'};
static char gpcMediaCurrState[BT_MAX_STR_LEN] = {'\0'};
static DBusConnection*  gpDBusConn = NULL;
static char* gpcBTAgentPath = NULL;
static char* gpcBTDAdapterPath = NULL;
static char* gpcBTAdapterPath = NULL;
static char* gpcDevTransportPath = NULL;
static void* gpcBDevStatusUserData = NULL;
static void* gpcBMediaStatusUserData = NULL;
static void* gpcBNegMediaUserData = NULL;
static void* gpcBTransPathMediaUserData = NULL;
static void* gpcBMediaPlayerPathUserData = NULL;
static void* gpcBConnIntimUserData = NULL;
static void* gpcBConnAuthUserData = NULL;

static unsigned int gpcBConnAuthPassKey = 0;
static unsigned int gpDevLost = 0;

static const DBusObjectPathVTable gDBusMediaEndpointVTable = {
    .message_function = btrCore_BTMediaEndpointHandler_cb,
};

static const DBusObjectPathVTable gDBusAgentVTable = {
    .message_function = btrCore_BTAgentMessageHandler_cb,
};

/* Callbacks */
static fPtr_BtrCore_BTDevStatusUpdate_cB    gfpcBDevStatusUpdate = NULL;
static fPtr_BtrCore_BTMediaStatusUpdate_cB  gfpcBMediaStatusUpdate = NULL;
static fPtr_BtrCore_BTNegotiateMedia_cB     gfpcBNegotiateMedia = NULL;
static fPtr_BtrCore_BTTransportPathMedia_cB gfpcBTransportPathMedia = NULL;
static fPtr_BtrCore_BTMediaPlayerPath_cB    gfpcBTMediaPlayerPath = NULL;
static fPtr_BtrCore_BTConnIntim_cB          gfpcBConnectionIntimation = NULL;
static fPtr_BtrCore_BTConnAuth_cB           gfpcBConnectionAuthentication = NULL;


/* Static Function Defs */
static inline int 
btrCore_BTHandleDusError (
    DBusError*  apDBusErr,
    int         aErrline, 
    const char* apErrfunc
) {
    if (dbus_error_is_set(apDBusErr)) {
        BTRCORELOG_ERROR ("%d\t: %s - DBus Error is %s\n", aErrline, apErrfunc, apDBusErr->message);
        dbus_error_free(apDBusErr);
        return 1;
    }
    return 0;
}


static const char*
btrCore_DBusType2Name (
    int ai32MessageType
) {
    switch (ai32MessageType) {
    case DBUS_MESSAGE_TYPE_SIGNAL:
      return "Signal ";
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      return "MethodCall";
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      return "MethodReturn";
    case DBUS_MESSAGE_TYPE_ERROR:
      return "Error";
    default:
      return "Unknown";
    }
}


static enBTDeviceType
btrCore_BTMapDevClasstoDevType(
    unsigned int    lui32Class
) {
    enBTDeviceType lenBtDevType = enBTDevStUnknown;

    if ((lui32Class & 0x200) || (lui32Class & 0x400)) {
       unsigned int ui32DevClassID = (lui32Class & 0xFFF);

       switch (ui32DevClassID){
         case enBTDCSmartPhone:
                               BTRCORELOG_DEBUG ("Its a enBTDevAudioSource\n");
                               lenBtDevType = enBTDevAudioSource;
                               break;
         case enBTDCWearableHeadset:
         case enBTDCLoudspeaker:
                               BTRCORELOG_DEBUG ("Its a enBTDevAudioSink\n");
                               lenBtDevType = enBTDevAudioSink;
                               break;
         default:
                               BTRCORELOG_DEBUG ("Its a enBTDevUnknown\n");                   
                               lenBtDevType = enBTDevUnknown;
       }
    }

    return lenBtDevType;
}


static DBusHandlerResult
btrCore_BTDBusConnectionFilter_cb (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    int             i32OpRet = -1;
    stBTDeviceInfo  lstBTDeviceInfo;
    stBTMediaInfo   lstBTMediaInfo;
    int             li32MessageType;
    const char*     lpcSender;
    const char*     lpcDestination;


    memset(&lstBTDeviceInfo, 0, sizeof(stBTDeviceInfo));
    memset(&lstBTMediaInfo, 0, sizeof(stBTMediaInfo));
    lstBTDeviceInfo.i32RSSI = INT_MIN;

    BTRCORELOG_DEBUG ("Connection Filter Activated....\n");

    if (!apDBusMsg) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }


    li32MessageType = dbus_message_get_type(apDBusMsg);
    lpcSender       = dbus_message_get_sender(apDBusMsg);
    lpcDestination  = dbus_message_get_destination(apDBusMsg);
  
    BTRCORELOG_INFO ("%s Sender=%s -> Dest=%s Path=%s; Interface=%s; Member=%s\n", 
                    btrCore_DBusType2Name(li32MessageType),
                    lpcSender ? lpcSender : "Null",
                    lpcDestination ? lpcDestination : "Null",
                    dbus_message_get_path(apDBusMsg), 
                    dbus_message_get_interface(apDBusMsg), 
                    dbus_message_get_member(apDBusMsg));


    if (li32MessageType == DBUS_MESSAGE_TYPE_ERROR) {
        const char* lpcError = dbus_message_get_error_name(apDBusMsg);
        BTRCORELOG_ERROR ("Error = %s\n", lpcError ? lpcError : NULL);

    }
    else if (dbus_message_is_signal(apDBusMsg, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        BTRCORELOG_DEBUG ("Property Changed!\n");

        DBusMessageIter lDBusMsgIter;
        const char*     lpcDBusIface = NULL;

        dbus_message_iter_init(apDBusMsg, &lDBusMsgIter);
        dbus_message_iter_get_basic(&lDBusMsgIter, &lpcDBusIface);
        dbus_message_iter_next(&lDBusMsgIter);

        if (dbus_message_iter_get_arg_type(&lDBusMsgIter) == DBUS_TYPE_ARRAY) {
            if (lpcDBusIface) {
                if (!strcmp(lpcDBusIface, BT_DBUS_BLUEZ_ADAPTER_PATH)) {
                    BTRCORELOG_INFO ("Property Changed! : %s\n", BT_DBUS_BLUEZ_ADAPTER_PATH);
                }
                else if (!strcmp(lpcDBusIface, BT_DBUS_BLUEZ_DEVICE_PATH)) {
                    BTRCORELOG_INFO ("Property Changed! : %s\n", BT_DBUS_BLUEZ_DEVICE_PATH);
                     i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, dbus_message_get_path(apDBusMsg));
                    
                     if (gfpcBDevStatusUpdate && !i32OpRet) {
                        enBTDeviceState lenBtDevState = enBTDevStUnknown; 
                        enBTDeviceType  lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);

                        if (lstBTDeviceInfo.bPaired) {
                            if (lstBTDeviceInfo.bConnected) {
                                const char* value = "connected";

                                strncpy(lstBTDeviceInfo.pcDevicePrevState, gpcDeviceCurrState, BT_MAX_STR_LEN - 1);
                                strncpy(lstBTDeviceInfo.pcDeviceCurrState, value, BT_MAX_STR_LEN - 1);
                                strncpy(gpcDeviceCurrState, value, BT_MAX_STR_LEN - 1);

                                lenBtDevState = enBTDevStPropChanged;
                            }
                            else if (!lstBTDeviceInfo.bConnected) {
                                const char* value = "disconnected";

                                strncpy(lstBTDeviceInfo.pcDevicePrevState, gpcDeviceCurrState, BT_MAX_STR_LEN - 1);
                                strncpy(lstBTDeviceInfo.pcDeviceCurrState, value, BT_MAX_STR_LEN - 1);
                                strncpy(gpcDeviceCurrState, value, BT_MAX_STR_LEN - 1);

                                lenBtDevState = enBTDevStPropChanged;
        
                                if (enBTDevAudioSink == lenBTDevType && gpDevLost) {
                                   lenBtDevState = enBTDevStLost;
                                }
                            }
                            gpDevLost = 0;

                            if(gfpcBDevStatusUpdate(lenBTDevType, lenBtDevState, &lstBTDeviceInfo, gpcBDevStatusUserData)) {
                            }
                        }
                        else if (!lstBTDeviceInfo.bPaired && !lstBTDeviceInfo.bConnected) {
                            lenBtDevState = enBTDevStFound;
                            if(gfpcBDevStatusUpdate(lenBTDevType, lenBtDevState, &lstBTDeviceInfo, gpcBDevStatusUserData)) {
                            }
                        }
                    }

                }
                else if (!strcmp(lpcDBusIface, BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH)) {
                    const char* apcMediaTransIface = dbus_message_get_path(apDBusMsg);
                    char apcDeviceIfce[BT_MAX_STR_LEN] = {'\0'};
                    unsigned int ui32DeviceIfceLen = strstr(apcMediaTransIface, "/fd") - apcMediaTransIface;
                    enBTDeviceType  lenBTDevType = enBTDevUnknown;
                    char* apcDevAddr   = 0;

                    BTRCORELOG_INFO ("Property Changed! : %s\n", BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH);
                    
                    i32OpRet = btrCore_BTGetMediaInfo(&lstBTMediaInfo, apcMediaTransIface);
 
                    if ((ui32DeviceIfceLen > 0) && (ui32DeviceIfceLen < (BT_MAX_STR_LEN - 1))) {
                       strncpy(apcDeviceIfce, apcMediaTransIface, ui32DeviceIfceLen);
                       i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, apcDeviceIfce);
                       if (!i32OpRet) {
                          lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);
                          apcDevAddr    = lstBTDeviceInfo.pcAddress;
                       }
                   }

                    if ((!strcmp(gpcMediaCurrState, "none")) && (!strcmp(lstBTMediaInfo.pcState, "pending"))) {
                        strcpy(gpcMediaCurrState, lstBTMediaInfo.pcState);

                        if (gfpcBDevStatusUpdate && !i32OpRet && lstBTDeviceInfo.bConnected) {
                            const char* value = "playing";
                            enBTDeviceState lenBtDevState = enBTDevStPropChanged; 

                            strncpy(lstBTDeviceInfo.pcDevicePrevState, gpcDeviceCurrState, BT_MAX_STR_LEN - 1);
                            strncpy(lstBTDeviceInfo.pcDeviceCurrState, value, BT_MAX_STR_LEN - 1);
                            strncpy(gpcDeviceCurrState, value, BT_MAX_STR_LEN - 1);

                            if(gfpcBDevStatusUpdate(lenBTDevType, lenBtDevState, &lstBTDeviceInfo, gpcBDevStatusUserData)) {
                            }
                          //if () {} t avmedia wiht device address info. from which id can be computed 
                        }
                    }
                    else 
                    if (enBTDevAudioSource == lenBTDevType) { //Lets handle AudioIn case for media events for now
                       char*  apcDevTransportPath = (char*)apcMediaTransIface;

                       // TODO: Obtain this data path reAcquire call from BTRCore - do as part of next commit
                       if (strcmp(gpcMediaCurrState, "none") && !strcmp(lstBTMediaInfo.pcState, "pending")) {
                          int    dataPathFd   = 0;
                          int    dataReadMTU  = 0;
                          int    dataWriteMTU = 0;

                          if (BtrCore_BTAcquireDevDataPath (gpDBusConn, apcDevTransportPath, &dataPathFd, &dataReadMTU, &dataWriteMTU)) {
                             BTRCORELOG_ERROR ("Failed to ReAcquire transport path %s", apcMediaTransIface);
                          } else {
                             BTRCORELOG_INFO  ("Successfully ReAcquired transport path %s", apcMediaTransIface);
                          }
                       }
                
                       if (gfpcBMediaStatusUpdate) {
                          enBTMediaTransportState  lenBtMTransportSt = enBTMTransportStNone;
                          stBTMediaStatusUpdate    mediaStatusUpdate; 

                          if (!strcmp(lstBTMediaInfo.pcState, "idle"))
                             lenBtMTransportSt = enBTMTransportStIdle;
                          else
                          if (!strcmp(lstBTMediaInfo.pcState, "pending"))
                             lenBtMTransportSt = enBTMTransportStPending;
                          else
                          if (!strcmp(lstBTMediaInfo.pcState, "active"))
                             lenBtMTransportSt = enBTMTransportStActive;

                          mediaStatusUpdate.aeBtMediaStatus       = enBTMediaTransportUpdate;
                          mediaStatusUpdate.m_mediaTransportState = lenBtMTransportSt;

                          //if(gfpcBMediaStatusUpdate(lenBTDevType, &mediaStatusUpdate, apcDevAddr, gpcBMediaStatusUserData)) {
                          //}
                          (void)mediaStatusUpdate;
                          (void)apcDevAddr;
                       }
                    }
                }
                else {
                    BTRCORELOG_INFO ("Property Changed! : %s\n", lpcDBusIface);
                }
            }
        }
    }
    else if (dbus_message_is_signal(apDBusMsg, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
        DBusMessageIter lDBusMsgIter;
        DBusMessageIter lDBusMsgIterDict;
        const char*     lpcDBusIface = NULL;

        dbus_message_iter_init(apDBusMsg, &lDBusMsgIter);
        if (dbus_message_iter_get_arg_type(&lDBusMsgIter) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&lDBusMsgIter, &lpcDBusIface);
            dbus_message_iter_next(&lDBusMsgIter);

            if (dbus_message_iter_get_arg_type(&lDBusMsgIter) == DBUS_TYPE_ARRAY) {
                BTRCORELOG_INFO ("InterfacesAdded : Interface %s\n", lpcDBusIface ? lpcDBusIface : NULL);


                dbus_message_iter_recurse(&lDBusMsgIter, &lDBusMsgIterDict);

                while (dbus_message_iter_get_arg_type(&lDBusMsgIterDict) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter lDBusMsgIterStrnArr;
                    const char*     lpcDBusIfaceInternal = NULL;

                    dbus_message_iter_recurse(&lDBusMsgIterDict, &lDBusMsgIterStrnArr);

                    if (dbus_message_iter_get_arg_type(&lDBusMsgIterStrnArr) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&lDBusMsgIterStrnArr, &lpcDBusIfaceInternal);

                        dbus_message_iter_next(&lDBusMsgIterStrnArr);
                        if (dbus_message_iter_get_arg_type(&lDBusMsgIterStrnArr) == DBUS_TYPE_ARRAY) {
                            if (lpcDBusIfaceInternal) {
                                if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_ADAPTER_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_ADAPTER_PATH);
                                }
                                else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_DEVICE_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_DEVICE_PATH);

                                    if (lpcDBusIface) {
                                        i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, lpcDBusIface);
                                         if (gfpcBDevStatusUpdate && !i32OpRet) {
                                            enBTDeviceState lenBtDevState = enBTDevStUnknown;
                                            enBTDeviceType  lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);

                                            if (!lstBTDeviceInfo.bPaired && !lstBTDeviceInfo.bConnected) {
                                                lenBtDevState = enBTDevStFound;
                                                if(gfpcBDevStatusUpdate(lenBTDevType, lenBtDevState, &lstBTDeviceInfo, gpcBDevStatusUserData)) {
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH);
                                }
                                else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH);

                                    if (lpcDBusIface && gfpcBTMediaPlayerPath) {
                                       if (!gfpcBTMediaPlayerPath(lpcDBusIface, gpcBMediaPlayerPathUserData)) {
                                          BTRCORELOG_ERROR ("Media Player Path callBack Failed!!!\n");
                                       } 
                                    }   
                                }
                                else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_ITEM_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_MEDIA_ITEM_PATH);
                                    char apcDeviceIfce[BT_MAX_STR_LEN] = {'\0'};
                                    unsigned int ui32DeviceIfceLen     = strstr(lpcDBusIface, "/player") - lpcDBusIface;
                                    enBTDeviceType  lenBTDevType       = enBTDevUnknown;
                                    char* apcDevAddr                   = 0;

                                    if ((ui32DeviceIfceLen > 0) && (ui32DeviceIfceLen < (BT_MAX_STR_LEN - 1))) {
                                       strncpy(apcDeviceIfce, lpcDBusIface, ui32DeviceIfceLen);
                                       i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, apcDeviceIfce);
                                       if (!i32OpRet) {
                                          lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);
                                          apcDevAddr    = lstBTDeviceInfo.pcAddress;
                                       }
                                    }

                                    if (strstr(lpcDBusIface, "item")) {
                                       BTRCORELOG_INFO ("MediaItem InterfacesAdded : %s\n", strstr(lpcDBusIface, "item"));

                                       if (gfpcBMediaStatusUpdate) {
                                          stBTMediaTrackInfo mediaTrackInfo;
                                          char apcMediaIfce[BT_MAX_STR_LEN] = {'\0'};
                                          unsigned int ui32MediaIfceLen     = strstr(lpcDBusIface, "/NowPlaying") - lpcDBusIface;

                                          if ((ui32MediaIfceLen > 0) && (ui32MediaIfceLen < (BT_MAX_STR_LEN - 1))) {
                                             strncpy(apcMediaIfce, lpcDBusIface, ui32MediaIfceLen);
                                          }

                                          if (!BtrCore_BTGetTrackInformation (gpDBusConn, apcMediaIfce, &mediaTrackInfo)) {
                                             stBTMediaStatusUpdate mediaStatusUpdate;

                                             mediaStatusUpdate.aeBtMediaStatus  = enBTMediaTrackUpdate;
                                             mediaStatusUpdate.m_mediaTrackInfo = &mediaTrackInfo;

                                             if(gfpcBMediaStatusUpdate(lenBTDevType, &mediaStatusUpdate, apcDevAddr, gpcBMediaStatusUserData)) {
                                             }
                                          }
                                       }    
                                    }
                                    else if (strstr(lpcDBusIface, "NowPlaying")) {
                                        BTRCORELOG_INFO ("MediaItem InterfacesAdded : %s\n", strstr(lpcDBusIface, "NowPlaying"));
                                    }
                                    else if (strstr(lpcDBusIface, "FileSystem")) {
                                        BTRCORELOG_INFO ("MediaItem InterfacesAdded : %s\n", strstr(lpcDBusIface, "FileSystem"));
                                    }
                                }
                                else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_FOLDER_PATH)) {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", BT_DBUS_BLUEZ_MEDIA_FOLDER_PATH);
                                }
                                else {
                                    BTRCORELOG_INFO ("InterfacesAdded : %s\n", lpcDBusIfaceInternal);
                                }
                            }
                        }
                    }

                    dbus_message_iter_next(&lDBusMsgIterDict);
                }
            }
        }
    }
    else if (dbus_message_is_signal(apDBusMsg, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved")) {
        BTRCORELOG_WARN ("Device Lost!\n");

        DBusMessageIter lDBusMsgIterStr;
        DBusMessageIter lDBusMsgIter;
        const char*     lpcDBusIface = NULL;

        dbus_message_iter_init(apDBusMsg, &lDBusMsgIter);
        dbus_message_iter_get_basic(&lDBusMsgIter, &lpcDBusIface);
        dbus_message_iter_next(&lDBusMsgIter);

        if (dbus_message_iter_get_arg_type(&lDBusMsgIter) == DBUS_TYPE_ARRAY) {
            BTRCORELOG_INFO ("InterfacesRemoved : Interface %s\n", lpcDBusIface ? lpcDBusIface : NULL);

            dbus_message_iter_recurse(&lDBusMsgIter, &lDBusMsgIterStr);

            while (dbus_message_iter_get_arg_type(&lDBusMsgIterStr) == DBUS_TYPE_STRING) {
                const char* lpcDBusIfaceInternal = NULL;

                dbus_message_iter_get_basic(&lDBusMsgIterStr, &lpcDBusIfaceInternal);

                if (lpcDBusIfaceInternal) {
                    if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_ADAPTER_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_ADAPTER_PATH);
                    }
                    else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_DEVICE_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_DEVICE_PATH);
                        
                        if (lpcDBusIface) {
                            i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, lpcDBusIface);
                            if (gfpcBDevStatusUpdate && !i32OpRet) {
                                enBTDeviceType  lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);
                                if(gfpcBDevStatusUpdate(lenBTDevType, enBTDevStUnPaired, &lstBTDeviceInfo, gpcBDevStatusUserData)) {
                                }
                            }
                        }
                    }
                    else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH);
                        // For Device Lost or Out Of Range cases                      
                        gpDevLost = 1;                        
                       
                        //TODO: What if some other devices transport interface gets removed with delay ? 
                        strncpy(gpcMediaCurrState, "none", BT_MAX_STR_LEN - 1); 
                    }
                    else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH);
                        // To free the player Path
                        if (lpcDBusIface && gfpcBTMediaPlayerPath) {
                           //if (!gfpcBTMediaPlayerPath(lpcDBusIface, gpcBMediaPlayerPathUserData)) {
                           //   BTRCORELOG_ERROR ("Media Player Path callBack Failed!!!\n");
                           //}
                        }  
                    }
                    else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_ITEM_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_MEDIA_ITEM_PATH);

                        if (strstr(lpcDBusIface, "item")) {
                           BTRCORELOG_INFO ("MediaItem InterfacesRemoved : %s\n", strstr(lpcDBusIface, "item"));
                        }
                        else if (strstr(lpcDBusIface, "NowPlaying")) {
                           BTRCORELOG_INFO ("MediaItem InterfacesRemoved : %s\n", strstr(lpcDBusIface, "NowPlaying"));
                        }
                        else if (strstr(lpcDBusIface, "FileSystem")) {
                           BTRCORELOG_INFO ("MediaItem InterfacesRemoved : %s\n", strstr(lpcDBusIface, "FileSystem"));
                        }
                    }
                    else if (!strcmp(lpcDBusIfaceInternal, BT_DBUS_BLUEZ_MEDIA_FOLDER_PATH)) {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", BT_DBUS_BLUEZ_MEDIA_FOLDER_PATH);
                    }
                    else {
                        BTRCORELOG_INFO ("InterfacesRemoved : %s\n", lpcDBusIfaceInternal);
                    }
                }

                dbus_message_iter_next(&lDBusMsgIterStr);
            }
        }
    }

    if (!i32OpRet)
        return DBUS_HANDLER_RESULT_HANDLED;
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static DBusHandlerResult
btrCore_BTMediaEndpointHandler_cb (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;
    const char*     lpcPath;

    lpcPath = dbus_message_get_path(apDBusMsg);

    (void)lpcPath;

    BTRCORELOG_INFO ("endpoint_handler: MediaEndpoint1\n");

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_MEDIA_ENDPOINT_PATH, "SelectConfiguration")) {
        BTRCORELOG_INFO ("endpoint_handler: MediaEndpoint1-SelectConfiguration\n");
        lpDBusReply = btrCore_BTMediaEndpointSelectConfiguration(apDBusMsg);
    }
    else if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_MEDIA_ENDPOINT_PATH, "SetConfiguration"))  {
        BTRCORELOG_INFO ("endpoint_handler: MediaEndpoint1-SetConfiguration\n");
        lpDBusReply = btrCore_BTMediaEndpointSetConfiguration(apDBusMsg);
    }
    else if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_MEDIA_ENDPOINT_PATH, "ClearConfiguration")) {
        BTRCORELOG_INFO ("endpoint_handler: MediaEndpoint1-ClearConfiguration\n");
        lpDBusReply = btrCore_BTMediaEndpointClearConfiguration(apDBusMsg);
    }
    else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (lpDBusReply) {
        dbus_connection_send(apDBusConn, lpDBusReply, NULL);
        dbus_message_unref(lpDBusReply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
btrCore_BTAgentMessageHandler_cb (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {

    BTRCORELOG_INFO ("btrCore_BTAgentMessageHandler_cb\n");

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "Release"))
        return btrCore_BTAgentRelease (apDBusConn, apDBusMsg, apvUserData);

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "RequestPinCode"))
        return btrCore_BTAgentRequestPincode(apDBusConn, apDBusMsg, apvUserData);

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "RequestPasskey"))
        return btrCore_BTAgentRequestPasskey(apDBusConn, apDBusMsg, apvUserData);

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "RequestConfirmation"))
        return btrCore_BTAgentRequestConfirmation(apDBusConn, apDBusMsg, apvUserData);

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "AuthorizeService"))
        return btrCore_BTAgentAuthorize(apDBusConn, apDBusMsg, apvUserData);

    if (dbus_message_is_method_call(apDBusMsg, BT_DBUS_BLUEZ_AGENT_PATH, "Cancel"))
        return btrCore_BTAgentCancelMessage(apDBusConn, apDBusMsg, apvUserData);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static char*
btrCore_BTGetDefaultAdapterPath (
    void
) {
    DBusMessage*    lpDBusReply = NULL;
    DBusMessageIter rootIter;
    int             a = 0;
    int             b = 0;
    bool            adapterFound = FALSE;
    char*           adapter_path;
    char            objectPath[256] = {'\0'};
    char            objectData[256] = {'\0'};


    lpDBusReply = btrCore_BTSendMethodCall("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");

    if (lpDBusReply && 
        dbus_message_iter_init(lpDBusReply, &rootIter) &&               //point iterator to lpDBusReply message
        DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&rootIter)) {  //get the type of message that iter points to

        DBusMessageIter arrayElementIter;
        dbus_message_iter_recurse(&rootIter, &arrayElementIter); //assign new iterator to first element of array

        while (!adapterFound) {

            if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&arrayElementIter)) {
                DBusMessageIter dictEntryIter;

                dbus_message_iter_recurse(&arrayElementIter,&dictEntryIter ); //assign new iterator to first element of (get all dict entries of 1st level (all object paths)
                if (DBUS_TYPE_OBJECT_PATH == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    dbus_message_iter_get_basic(&dictEntryIter, &adapter_path);
                    strcpy(objectPath, adapter_path);
                    ++a;
                }

                dbus_message_iter_next(&dictEntryIter);
                if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    DBusMessageIter innerArrayIter;

                    dbus_message_iter_recurse(&dictEntryIter, &innerArrayIter);

                    while (dbus_message_iter_has_next(&innerArrayIter)) {

                        if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter)) {
                            DBusMessageIter innerDictEntryIter;
                            dbus_message_iter_recurse(&innerArrayIter,&innerDictEntryIter ); //assign new iterator to first element of

                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                char *dbusObject;
                                dbus_message_iter_get_basic(&innerDictEntryIter, &dbusObject);

                                ////// getting default adapter path //////

                                if (strcmp(dbusObject, BT_DBUS_BLUEZ_ADAPTER_PATH) == 0) {
                                    gpcBTDAdapterPath = strdup(adapter_path);
                                    adapterFound = TRUE;
                                    break;
                                }
                            }

                            /////// NEW //////////
                            dbus_message_iter_next(&innerDictEntryIter);
                            if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                DBusMessageIter innerArrayIter2;
                                dbus_message_iter_recurse(&innerDictEntryIter, &innerArrayIter2);

                                while (dbus_message_iter_has_next(&innerArrayIter2)) {
                                    if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter2)) {
                                        DBusMessageIter innerDictEntryIter2;

                                        dbus_message_iter_recurse(&innerArrayIter2,&innerDictEntryIter2); //assign new iterator to first element of
                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter2)) {
                                            char *dbusObject2;
                                            dbus_message_iter_get_basic(&innerDictEntryIter2, &dbusObject2);
                                        }

                                        ////////////// NEW 2 ////////////
                                        dbus_message_iter_next(&innerDictEntryIter2);
                                        DBusMessageIter innerDictEntryIter3;
                                        char *dbusObject3;

                                        dbus_message_iter_recurse(&innerDictEntryIter2,&innerDictEntryIter3);
                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &dbusObject3);
                                            strcpy(objectData, dbusObject3);
                                            ++b;
                                        }
                                        else if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            bool *device_prop = FALSE;
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &device_prop);
                                        }

                                    }

                                    if (!dbus_message_iter_has_next(&innerArrayIter2)) {
                                        break; //check to see if end of 3rd array
                                    }
                                    else {
                                        dbus_message_iter_next(&innerArrayIter2);
                                    }
                                }
                            }
                        }

                        if (!dbus_message_iter_has_next(&innerArrayIter)) {
                            break; //check to see if end of 2nd array
                        }
                        else {
                            dbus_message_iter_next(&innerArrayIter);
                        }
                    }
                }

                if (!dbus_message_iter_has_next(&arrayElementIter)) {
                    break; //check to see if end of 1st array
                }
                else {
                    dbus_message_iter_next(&arrayElementIter);
                }
            } //while loop end --used to traverse arra
        }

        dbus_message_unref(lpDBusReply);
    }

    if (gpcBTDAdapterPath) {
        BTRCORELOG_DEBUG ("\n\nDefault Adpater Path is: %s\n", gpcBTDAdapterPath);
    }
    return gpcBTDAdapterPath;
}


static int
btrCore_BTReleaseDefaultAdapterPath (
    void
) {
    if (gpcBTDAdapterPath) {
        free(gpcBTDAdapterPath);
        gpcBTDAdapterPath = NULL;
    }

    return 0;
}


static DBusHandlerResult
btrCore_BTAgentRelease (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;

    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for Release method");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    lpDBusReply = dbus_message_new_method_return(apDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Unable to create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    dbus_connection_send(apDBusConn, lpDBusReply, NULL);
    dbus_connection_flush(apDBusConn);

    dbus_message_unref(lpDBusReply);
       //return the result
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
btrCore_BTAgentRequestPincode (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;
    const char*     lpcPath     = NULL;

    if (!gpcBTOutPassCode)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_OBJECT_PATH, &lpcPath, DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for RequestPinCode method");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (do_reject) {
        lpDBusReply = dbus_message_new_error(apDBusMsg, "org.bluez.Error.Rejected", "");
        goto sendmsg;
    }

    lpDBusReply = dbus_message_new_method_return(apDBusMsg);
    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    BTRCORELOG_INFO ("Pincode request for device %s\n", lpcPath);
    dbus_message_append_args(lpDBusReply, DBUS_TYPE_STRING, &gpcBTOutPassCode, DBUS_TYPE_INVALID);

sendmsg:
    dbus_connection_send(apDBusConn, lpDBusReply, NULL);
    dbus_connection_flush(apDBusConn);

    dbus_message_unref(lpDBusReply);

    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
btrCore_BTAgentRequestPasskey (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;
    const char*     lpcPath     = NULL;
    unsigned int    ui32PassCode= 0;

    if (!gpcBTOutPassCode)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_OBJECT_PATH, &lpcPath, DBUS_TYPE_INVALID))  {
        BTRCORELOG_ERROR ("Incorrect args btrCore_BTAgentRequestPasskey");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    lpDBusReply = dbus_message_new_method_return(apDBusMsg);
    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    BTRCORELOG_INFO ("Pass code request for device %s\n", lpcPath);
    ui32PassCode = strtoul(gpcBTOutPassCode, NULL, 10);
    dbus_message_append_args(lpDBusReply, DBUS_TYPE_UINT32, &ui32PassCode, DBUS_TYPE_INVALID);

    dbus_connection_send(apDBusConn, lpDBusReply, NULL);
    dbus_connection_flush(apDBusConn);
    dbus_message_unref(lpDBusReply);

    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult
btrCore_BTAgentRequestConfirmation (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;
    const char*     lpcPath     = NULL;
    unsigned int    ui32PassCode= 0;
    int             yesNo       = 0;
    int             i32OpRet    = -1;
    stBTDeviceInfo  lstBTDeviceInfo;

    memset(&lstBTDeviceInfo, 0, sizeof(stBTDeviceInfo));


    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_OBJECT_PATH, &lpcPath, DBUS_TYPE_UINT32, &ui32PassCode, DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for Authorize method");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }


    BTRCORELOG_INFO ("btrCore_BTAgentRequestConfirmation: PASS Code for %s is %6d\n", lpcPath, ui32PassCode);

    if (gfpcBConnectionIntimation && lpcPath) {
        i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, lpcPath);
        enBTDeviceType  lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);

        BTRCORELOG_INFO ("calling ConnIntimation cb for %s - OpRet = %d\n", lpcPath, i32OpRet);
        yesNo = gfpcBConnectionIntimation(lenBTDevType, &lstBTDeviceInfo, ui32PassCode, gpcBConnIntimUserData);
    }

    gpcBConnAuthPassKey = ui32PassCode;


    if (yesNo == 0) {
        BTRCORELOG_ERROR ("Sorry, you cant connect....\n");
        lpDBusReply = dbus_message_new_error(apDBusMsg, "org.bluez.Error.Rejected", "");
    }
    else {
        lpDBusReply = dbus_message_new_method_return(apDBusMsg);
    }


    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    else {
        BTRCORELOG_INFO ("Intimating request for %s\n", lpcPath);
        dbus_connection_send(apDBusConn, lpDBusReply, NULL);
        dbus_connection_flush(apDBusConn);
        dbus_message_unref(lpDBusReply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
}


static DBusHandlerResult
btrCore_BTAgentAuthorize (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;
    const char*     lpcPath     = NULL;
    const char*     uuid        = NULL;
    int             yesNo       = 0;
    int             i32OpRet    = -1;
    stBTDeviceInfo  lstBTDeviceInfo;

    memset(&lstBTDeviceInfo, 0, sizeof(stBTDeviceInfo));


    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_OBJECT_PATH, &lpcPath, DBUS_TYPE_STRING, &uuid, DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for Authorize method");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (gfpcBConnectionAuthentication && lpcPath) {
        i32OpRet = btrCore_BTGetDeviceInfo(&lstBTDeviceInfo, lpcPath);
        enBTDeviceType  lenBTDevType  = btrCore_BTMapDevClasstoDevType(lstBTDeviceInfo.ui32Class);

        BTRCORELOG_INFO ("calling ConnAuth cb for %s - OpRet = %d\n", lpcPath, i32OpRet);
        yesNo = gfpcBConnectionAuthentication(lenBTDevType, &lstBTDeviceInfo, gpcBConnAuthUserData);
    }

    gpcBConnAuthPassKey = 0;


    if (yesNo == 0) {
        BTRCORELOG_ERROR ("Sorry, you cant connect....\n");
        lpDBusReply = dbus_message_new_error(apDBusMsg, "org.bluez.Error.Rejected", "");
    }
    else {
        lpDBusReply = dbus_message_new_method_return(apDBusMsg);
    }


    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    else {
        BTRCORELOG_INFO ("Authorizing request for %s\n", lpcPath);
        dbus_connection_send(apDBusConn, lpDBusReply, NULL);
        dbus_connection_flush(apDBusConn);
        dbus_message_unref(lpDBusReply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
}


static DBusHandlerResult
btrCore_BTAgentCancelMessage (
    DBusConnection* apDBusConn,
    DBusMessage*    apDBusMsg,
    void*           apvUserData
) {
    DBusMessage*    lpDBusReply = NULL;

    if (!dbus_message_get_args(apDBusMsg, NULL, DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for confirmation method");
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    BTRCORELOG_INFO ("Request canceled\n");
    lpDBusReply = dbus_message_new_method_return(apDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't create lpDBusReply message\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    dbus_connection_send(apDBusConn, lpDBusReply, NULL);
    dbus_connection_flush(apDBusConn);

    dbus_message_unref(lpDBusReply);
    return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusMessage*
btrCore_BTSendMethodCall (
    const char*     objectpath,
    const char*     interfacename,
    const char*     methodname
) {
    const char*     busname = BT_DBUS_BLUEZ_PATH;

    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusPendingCall*    lpDBusPendC = NULL;


    lpDBusMsg = dbus_message_new_method_call(busname,
                                             objectpath,
                                             interfacename,
                                             methodname);

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Cannot allocate DBus message!\n");
        return NULL;
    }

    //Now do a sync call
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) { //Send and expect lpDBusReply using pending call object
        BTRCORELOG_ERROR ("failed to send message!\n");
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);
    lpDBusMsg = NULL;

    dbus_pending_call_block(lpDBusPendC);                       //Now block on the pending call
    lpDBusReply = dbus_pending_call_steal_reply(lpDBusPendC);   //Get the lpDBusReply message from the queue
    dbus_pending_call_unref(lpDBusPendC);                       //Free pending call handle

    if (dbus_message_get_type(lpDBusReply) ==  DBUS_MESSAGE_TYPE_ERROR) {
        BTRCORELOG_ERROR ("Error : %s\n\n", dbus_message_get_error_name(lpDBusReply));
        dbus_message_unref(lpDBusReply);
        lpDBusReply = NULL;
    }

    return lpDBusReply;
}


static int
btrCore_BTGetDeviceInfo (
    stBTDeviceInfo* apstBTDeviceInfo,
    const char*     apcIface
) {
    char*               pdeviceInterface = BT_DBUS_BLUEZ_DEVICE_PATH;
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusMessageIter     args;
    DBusError           lDBusErr;
    DBusPendingCall*    lpDBusPendC;


    if (!apcIface)
        return -1;

    BTRCORELOG_DEBUG ("Getting properties for the device %s\n", apcIface);

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcIface,
                                             "org.freedesktop.DBus.Properties",
                                             "GetAll");

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pdeviceInterface, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
        BTRCORELOG_ERROR ("failed to send message");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);
    lpDBusMsg = NULL;

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
    dbus_pending_call_unref(lpDBusPendC);

    if (lpDBusReply != NULL) {
        if (0 != btrCore_BTParseDevice(lpDBusReply, apstBTDeviceInfo)) {
            BTRCORELOG_ERROR ("Parsing the device %s failed..\n", apcIface);
            dbus_message_unref(lpDBusReply);
            return -1;
        }
        else {
            dbus_message_unref(lpDBusReply);
            return 0;
        }
    }

    dbus_message_unref(lpDBusReply);
    return 0;
}


static int
btrCore_BTParseDevice (
    DBusMessage*    apDBusMsg,
    stBTDeviceInfo* apstBTDeviceInfo
) {
    DBusMessageIter arg_i;
    DBusMessageIter element_i;
    DBusMessageIter variant_i;
    int             dbus_type;

    const char*     pcKey = NULL;
    int             bPaired = 0;
    int             bConnected = 0;
    int             bTrusted = 0;
    int             bBlocked = 0;
    unsigned short  ui16Vendor = 0;
    unsigned short  ui16VendorSource = 0;
    unsigned short  ui16Product = 0;
    unsigned short  ui16Version = 0;
    unsigned int    ui32Class = 0;
    short           i16RSSI = 0;
    const char*     pcName = NULL;
    const char*     pcAddress = NULL;
    const char*     pcAlias = NULL;
    const char*     pcIcon = NULL;

    if (!dbus_message_iter_init(apDBusMsg, &arg_i)) {
        BTRCORELOG_ERROR ("dbus_message_iter_init Failed\n");
        return -1;
    }

    dbus_type = dbus_message_iter_get_arg_type(&arg_i);
    if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
        dbus_message_iter_next(&arg_i);
        dbus_type = dbus_message_iter_get_arg_type(&arg_i);

        if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
            BTRCORELOG_ERROR ("Unknown Prop structure from Bluez\n");
            return -1;
        }
    }

    dbus_message_iter_recurse(&arg_i, &element_i);
    while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
        if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter dict_i;

            dbus_message_iter_recurse(&element_i, &dict_i);
            dbus_message_iter_get_basic(&dict_i, &pcKey);

            if (strcmp (pcKey, "Address") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcAddress);
                strncpy(apstBTDeviceInfo->pcAddress, pcAddress, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTDeviceInfo->pcAddress : %s\n", apstBTDeviceInfo->pcAddress);
            }
            else if (strcmp (pcKey, "Name") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcName);
                strncpy(apstBTDeviceInfo->pcName, pcName, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTDeviceInfo->pcName: %s\n", apstBTDeviceInfo->pcName);

            }
            else if (strcmp (pcKey, "Vendor") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16Vendor);
                apstBTDeviceInfo->ui16Vendor = ui16Vendor;
                BTRCORELOG_INFO ("apstBTDeviceInfo->ui16Vendor = %d\n", apstBTDeviceInfo->ui16Vendor);
            }
            else if (strcmp (pcKey, "VendorSource") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16VendorSource);
                apstBTDeviceInfo->ui16VendorSource = ui16VendorSource;
                BTRCORELOG_INFO ("apstBTDeviceInfo->ui16VendorSource = %d\n", apstBTDeviceInfo->ui16VendorSource);
            }
            else if (strcmp (pcKey, "Product") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16Product);
                apstBTDeviceInfo->ui16Product = ui16Product;
                BTRCORELOG_INFO ("apstBTDeviceInfo->ui16Product = %d\n", apstBTDeviceInfo->ui16Product);
            }
            else if (strcmp (pcKey, "Version") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16Version);
                apstBTDeviceInfo->ui16Version = ui16Version;
                BTRCORELOG_INFO ("apstBTDeviceInfo->ui16Version = %d\n", apstBTDeviceInfo->ui16Version);
            }
            else if (strcmp (pcKey, "Icon") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcIcon);
                strncpy(apstBTDeviceInfo->pcIcon, pcIcon, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTDeviceInfo->pcIcon: %s\n", apstBTDeviceInfo->pcIcon);
            }
            else if (strcmp (pcKey, "Class") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui32Class);
                apstBTDeviceInfo->ui32Class = ui32Class;
                BTRCORELOG_INFO ("apstBTDeviceInfo->ui32Class: %d\n", apstBTDeviceInfo->ui32Class);
            }
            else if (strcmp (pcKey, "Paired") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &bPaired);
                apstBTDeviceInfo->bPaired = bPaired;
                BTRCORELOG_INFO ("apstBTDeviceInfo->bPaired = %d\n", apstBTDeviceInfo->bPaired);
            }
            else if (strcmp (pcKey, "Connected") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &bConnected);
                apstBTDeviceInfo->bConnected = bConnected;
                BTRCORELOG_INFO ("apstBTDeviceInfo->bConnected = %d\n", apstBTDeviceInfo->bConnected);
            }
            else if (strcmp (pcKey, "Trusted") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &bTrusted);
                apstBTDeviceInfo->bTrusted = bTrusted;
                BTRCORELOG_INFO ("apstBTDeviceInfo->bTrusted = %d\n", apstBTDeviceInfo->bTrusted);
            }
            else if (strcmp (pcKey, "Blocked") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &bBlocked);
                apstBTDeviceInfo->bBlocked = bBlocked;
                BTRCORELOG_INFO ("apstBTDeviceInfo->bBlocked = %d\n", apstBTDeviceInfo->bBlocked);
            }
            else if (strcmp (pcKey, "Alias") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcAlias);
                strncpy(apstBTDeviceInfo->pcAlias, pcAlias, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTDeviceInfo->pcAlias: %s\n", apstBTDeviceInfo->pcAlias);
            }
            else if (strcmp (pcKey, "RSSI") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &i16RSSI);
                apstBTDeviceInfo->i32RSSI = i16RSSI;
                BTRCORELOG_INFO ("apstBTDeviceInfo->i32RSSI = %d\n", apstBTDeviceInfo->i32RSSI);
            }
            else if (strcmp (pcKey, "UUIDs") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);

                dbus_type = dbus_message_iter_get_arg_type (&variant_i);
                if (dbus_type == DBUS_TYPE_ARRAY) {
                    int count = 0;
                    DBusMessageIter variant_j;
                    dbus_message_iter_recurse(&variant_i, &variant_j);

                    while ((dbus_type = dbus_message_iter_get_arg_type (&variant_j)) != DBUS_TYPE_INVALID) {
                        if ((dbus_type == DBUS_TYPE_STRING) && (count < BT_MAX_DEVICE_PROFILE)) {
                            char *pVal = NULL;
                            dbus_message_iter_get_basic (&variant_j, &pVal);
                            BTRCORELOG_INFO ("UUID value is %s\n", pVal);
                            strncpy(apstBTDeviceInfo->aUUIDs[count], pVal, (BT_MAX_UUID_STR_LEN - 1));
                            count++;
                        }
                        dbus_message_iter_next (&variant_j);
                    }
                }
                else {
                    BTRCORELOG_ERROR ("apstBTDeviceInfo->Services; Not an Array\n");
                }
            }
        }

        if (!dbus_message_iter_next(&element_i)) {
            break;
        }
    }

    (void)dbus_type;

    if (strlen(apstBTDeviceInfo->pcAlias))
        strncpy(apstBTDeviceInfo->pcName, apstBTDeviceInfo->pcAlias, strlen(apstBTDeviceInfo->pcAlias));

    return 0;
}

#if 0
static int
btrCore_BTParsePropertyChange (
    DBusMessage*    apDBusMsg,
    stBTDeviceInfo* apstBTDeviceInfo
) {
     DBusMessageIter arg_i, variant_i;
    const char* value;
    const char* bd_addr;
    int dbus_type;

    if (!dbus_message_iter_init(apDBusMsg, &arg_i)) {
       BTRCORELOG_ERROR ("GetProperties lpDBusReply has no arguments.");
    }

    if (!dbus_message_get_args( apDBusMsg, NULL,
                                DBUS_TYPE_STRING, &bd_addr,
                                DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid arguments for NameOwnerChanged signal");
        return -1;
    }

    BTRCORELOG_ERROR (" Name: %s\n",bd_addr);//"State" then the variant is a string
    if (strcmp(bd_addr,"State") == 0) {
        dbus_type = dbus_message_iter_get_arg_type(&arg_i);
        //BTRCORELOG_ERROR ("type is %d\n", dbus_type);

        if (dbus_type == DBUS_TYPE_STRING) {
            dbus_message_iter_next(&arg_i);
            dbus_message_iter_recurse(&arg_i, &variant_i);
            dbus_message_iter_get_basic(&variant_i, &value);
             // BTRCORELOG_ERROR ("    the new state is: %s\n", value);
            strncpy(apstBTDeviceInfo->pcDevicePrevState, gpcDeviceCurrState, BT_MAX_STR_LEN - 1);
            strncpy(apstBTDeviceInfo->pcDeviceCurrState, value, BT_MAX_STR_LEN - 1);
            strncpy(gpcDeviceCurrState, value, BT_MAX_STR_LEN - 1);
        }
    }

    return 0;
}
#endif


static int
btrCore_BTGetMediaInfo (
    stBTMediaInfo*  apstBTMediaInfo,
    const char*     apcIface
) {
    char*               pdeviceInterface = BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH;
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusMessageIter     args;
    DBusError           lDBusErr;
    DBusPendingCall*    lpDBusPendC;


    if (!apcIface)
        return -1;

    BTRCORELOG_DEBUG ("Getting properties for the Media Ifce %s\n", apcIface);

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcIface,
                                             "org.freedesktop.DBus.Properties",
                                             "GetAll");

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pdeviceInterface, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
        BTRCORELOG_ERROR ("failed to send message");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);
    lpDBusMsg = NULL;

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
    dbus_pending_call_unref(lpDBusPendC);

    if (lpDBusReply != NULL) {
        if (0 != btrCore_BTParseMediaTransport(lpDBusReply, apstBTMediaInfo)) {
            BTRCORELOG_ERROR ("Parsing the device %s failed..\n", apcIface);
            dbus_message_unref(lpDBusReply);
            return -1;
        }
        else {
            dbus_message_unref(lpDBusReply);
            return 0;
        }
    }

    dbus_message_unref(lpDBusReply);
    return 0;

}


static int
btrCore_BTParseMediaTransport (
    DBusMessage*    apDBusMsg,
    stBTMediaInfo*  apstBTMediaInfo
) {
    DBusMessageIter arg_i;
    DBusMessageIter element_i;
    DBusMessageIter variant_i;
    int             dbus_type;

    char*           pcKey = NULL;
    unsigned char   ui8Codec = 0;
    char*           pcState = NULL;
    char*           pcUUID = NULL;
    unsigned short  ui16Delay = 0;
    unsigned short  ui16Volume = 0;

    if (!dbus_message_iter_init(apDBusMsg, &arg_i)) {
        BTRCORELOG_ERROR ("dbus_message_iter_init Failed\n");
        return -1;
    }

    dbus_type = dbus_message_iter_get_arg_type(&arg_i);
    if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
        dbus_message_iter_next(&arg_i);
        dbus_type = dbus_message_iter_get_arg_type(&arg_i);

        if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
            BTRCORELOG_ERROR ("Unknown Prop structure from Bluez\n");
            return -1;
        }
    }

    dbus_message_iter_recurse(&arg_i, &element_i);
    while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
        if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter dict_i;

            dbus_message_iter_recurse(&element_i, &dict_i);
            dbus_message_iter_get_basic(&dict_i, &pcKey);

            if (strcmp (pcKey, "Codec") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui8Codec);
                apstBTMediaInfo->ui8Codec = ui8Codec;
                BTRCORELOG_INFO ("apstBTMediaInfo->ui8Codec : %d\n", apstBTMediaInfo->ui8Codec);
            }
            else if (strcmp (pcKey, "State") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcState);
                strncpy(apstBTMediaInfo->pcState, pcState, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTMediaInfo->pcState: %s\n", apstBTMediaInfo->pcState);

            }
            else if (strcmp (pcKey, "UUID") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &pcUUID);
                strncpy(apstBTMediaInfo->pcUUID, pcUUID, BT_MAX_STR_LEN);
                BTRCORELOG_INFO ("apstBTMediaInfo->pcUUID: %s\n", apstBTMediaInfo->pcUUID);
            }
            else if (strcmp (pcKey, "Delay") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16Delay);
                apstBTMediaInfo->ui16Delay = ui16Delay;
                BTRCORELOG_INFO ("apstBTMediaInfo->ui16Delay = %d\n", apstBTMediaInfo->ui16Delay);
            }
            else if (strcmp (pcKey, "Volume") == 0) {
                dbus_message_iter_next(&dict_i);
                dbus_message_iter_recurse(&dict_i, &variant_i);
                dbus_message_iter_get_basic(&variant_i, &ui16Volume);
                apstBTMediaInfo->ui16Volume = ui16Volume;
                BTRCORELOG_INFO ("apstBTMediaInfo->ui16Volume = %d\n", apstBTMediaInfo->ui16Volume);
            }
        }

        if (!dbus_message_iter_next(&element_i)) {
            break;
        }
    }

    (void)dbus_type;

    return 0;
}


static DBusMessage*
btrCore_BTMediaEndpointSelectConfiguration (
    DBusMessage*    apDBusMsg
) {
    DBusMessage*    lpDBusReply      = NULL;
    DBusError       lDBusErr;
    void*           lpInputMediaCaps = NULL;
    void*           lpOutputMediaCaps= NULL;
    int             lDBusArgsSize;


    dbus_error_init(&lDBusErr);

    if (!dbus_message_get_args(apDBusMsg, &lDBusErr, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &lpInputMediaCaps, &lDBusArgsSize, DBUS_TYPE_INVALID)) {
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to select configuration");
    }

    if (gfpcBNegotiateMedia) {
        if(!(lpOutputMediaCaps = gfpcBNegotiateMedia(lpInputMediaCaps, gpcBNegMediaUserData))) {
            return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to select configuration");
        }
    }

    lpDBusReply = dbus_message_new_method_return(apDBusMsg);
    dbus_message_append_args (lpDBusReply, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &lpOutputMediaCaps, lDBusArgsSize, DBUS_TYPE_INVALID);

    return lpDBusReply;
}


static DBusMessage*
btrCore_BTMediaEndpointSetConfiguration (
    DBusMessage*    apDBusMsg
) {
    const char*     lDevTransportPath = NULL;
    const char*     lStoredDevTransportPath = NULL;
    const char*     dev_path = NULL;
    const char*     uuid = NULL;
    unsigned char*  config = NULL;
    int             size = 0;

    DBusMessageIter lDBusMsgIter;
    DBusMessageIter lDBusMsgIterProp;
    DBusMessageIter lDBusMsgIterEntry;
    DBusMessageIter lDBusMsgIterValue;
    DBusMessageIter lDBusMsgIterArr;


    dbus_message_iter_init(apDBusMsg, &lDBusMsgIter);
    dbus_message_iter_get_basic(&lDBusMsgIter, &lDevTransportPath);
    if (!dbus_message_iter_next(&lDBusMsgIter))
        return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to set configuration");

    dbus_message_iter_recurse(&lDBusMsgIter, &lDBusMsgIterProp);
    if (dbus_message_iter_get_arg_type(&lDBusMsgIterProp) != DBUS_TYPE_DICT_ENTRY)
        return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to set configuration");

    while (dbus_message_iter_get_arg_type(&lDBusMsgIterProp) == DBUS_TYPE_DICT_ENTRY) {
        const char *key;
        int ldBusType;

        dbus_message_iter_recurse(&lDBusMsgIterProp, &lDBusMsgIterEntry);
        dbus_message_iter_get_basic(&lDBusMsgIterEntry, &key);

        dbus_message_iter_next(&lDBusMsgIterEntry);
        dbus_message_iter_recurse(&lDBusMsgIterEntry, &lDBusMsgIterValue);

        ldBusType = dbus_message_iter_get_arg_type(&lDBusMsgIterValue);
        if (strcasecmp(key, "UUID") == 0) {
            if (ldBusType != DBUS_TYPE_STRING)
                return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to set configuration");

            dbus_message_iter_get_basic(&lDBusMsgIterValue, &uuid);
        }
        else if (strcasecmp(key, "Device") == 0) {
            if (ldBusType != DBUS_TYPE_OBJECT_PATH)
                return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to set configuration");

            dbus_message_iter_get_basic(&lDBusMsgIterValue, &dev_path);
        }
        else if (strcasecmp(key, "Configuration") == 0) {
            if (ldBusType != DBUS_TYPE_ARRAY)
                return dbus_message_new_error(apDBusMsg, "org.bluez.MediaEndpoint1.Error.InvalidArguments", "Unable to set configuration");

            dbus_message_iter_recurse(&lDBusMsgIterValue, &lDBusMsgIterArr);
            dbus_message_iter_get_fixed_array(&lDBusMsgIterArr, &config, &size);
        }
        dbus_message_iter_next(&lDBusMsgIterProp);
    }

    BTRCORELOG_INFO ("Set configuration - Transport Path %s\n", lDevTransportPath);

    if (gpcDevTransportPath) {
        free(gpcDevTransportPath);
        gpcDevTransportPath = NULL;
    }

    gpcDevTransportPath = strdup(lDevTransportPath);

    if (gfpcBTransportPathMedia) {
        if((lStoredDevTransportPath = gfpcBTransportPathMedia(lDevTransportPath, config, gpcBTransPathMediaUserData))) {
            BTRCORELOG_INFO ("Stored - Transport Path 0x%8x:%s\n", (unsigned int)lStoredDevTransportPath, lStoredDevTransportPath);
        }
    }

    return dbus_message_new_method_return(apDBusMsg);
}


static DBusMessage*
btrCore_BTMediaEndpointClearConfiguration (
    DBusMessage*    apDBusMsg
) {

    DBusMessage*    lpDBusReply;
    DBusError       lDBusErr;
    DBusMessageIter lDBusMsgIter;
    const char*     lDevTransportPath = NULL;
    const char*     lStoredDevTransportPath = NULL;

    dbus_error_init(&lDBusErr);
    dbus_message_iter_init(apDBusMsg, &lDBusMsgIter);
    dbus_message_iter_get_basic(&lDBusMsgIter, &lDevTransportPath);
    BTRCORELOG_DEBUG ("Clear configuration - Transport Path %s\n", lDevTransportPath);

    if (gpcDevTransportPath) {
        free(gpcDevTransportPath);
        gpcDevTransportPath = NULL;
    }

    if (gfpcBTransportPathMedia) {
        if(!(lStoredDevTransportPath = gfpcBTransportPathMedia(lDevTransportPath, NULL, gpcBTransPathMediaUserData))) {
            BTRCORELOG_INFO ("Cleared - Transport Path %s\n", lDevTransportPath);
        }
    }

    lpDBusReply = dbus_message_new_method_return(apDBusMsg);

    return lpDBusReply;
}


static void 
btrCore_BTConnectDeviceConfirmation_cb (
     DBusPendingCall*   pending_call, 
     void*              data
) {
    DBusMessage *lDBusReply = NULL;

    BTRCORELOG_INFO ("Received Connect Device Confirmation\n");

    lDBusReply = dbus_pending_call_steal_reply(pending_call);

    dbus_pending_call_unref(pending_call);

    if (dbus_message_get_type(lDBusReply) == DBUS_MESSAGE_TYPE_ERROR) {
       BTRCORELOG_ERROR ("Connection Failure Reason : %s!!!", dbus_message_get_error_name(lDBusReply));
    }

    dbus_message_unref(lDBusReply);

    /* Can try pre and post ConnectDevice Settings at BTRCore and BTRMgr level based on this confirmation */
}


/* Interfaces */
void*
BtrCore_BTInitGetConnection (
    void
) {
    DBusError       lDBusErr;
    DBusConnection* lpDBusConn = NULL;

    dbus_error_init(&lDBusErr);
    lpDBusConn = dbus_bus_get(DBUS_BUS_SYSTEM, &lDBusErr);

    if (lpDBusConn == NULL) {
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return NULL;
    }

    BTRCORELOG_INFO ("DBus Debug DBus Connection Name %s\n", dbus_bus_get_unique_name (lpDBusConn));
    gpDBusConn = lpDBusConn;

    strncpy(gpcDeviceCurrState, "disconnected", BT_MAX_STR_LEN - 1);
    strncpy(gpcMediaCurrState, "none", BT_MAX_STR_LEN - 1); 

    if (!dbus_connection_add_filter(gpDBusConn, btrCore_BTDBusConnectionFilter_cb, NULL, NULL)) {
        BTRCORELOG_ERROR ("Can't add signal filter - BtrCore_BTInitGetConnection\n");
        BtrCore_BTDeInitReleaseConnection(lpDBusConn);
        return NULL;
    }

    dbus_bus_add_match(gpDBusConn, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'"",arg0='" BT_DBUS_BLUEZ_PATH "'", NULL);
    dbus_bus_add_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'", NULL);
    dbus_bus_add_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved'", NULL);
    dbus_bus_add_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_ADAPTER_PATH "'", NULL);
    dbus_bus_add_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_DEVICE_PATH "'", NULL);
    dbus_bus_add_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH "'", NULL);

    gpcBConnAuthPassKey             = 0;

    gpcBTransPathMediaUserData      = NULL;
    gpcBNegMediaUserData            = NULL;
    gpcBConnIntimUserData           = NULL;
    gpcBConnAuthUserData            = NULL;
    gpcBDevStatusUserData           = NULL;
    gfpcBDevStatusUpdate            = NULL;
    gfpcBNegotiateMedia             = NULL;
    gfpcBTransportPathMedia         = NULL;
    gfpcBConnectionIntimation       = NULL;
    gfpcBConnectionAuthentication   = NULL;

    return (void*)gpDBusConn;
}


int
BtrCore_BTDeInitReleaseConnection (
    void* apBtConn
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;


    if (gpcBTAgentPath) {
        free(gpcBTAgentPath);
        gpcBTAgentPath = NULL;
    }

    if (gpcBTDAdapterPath) {
        free(gpcBTDAdapterPath);
        gpcBTDAdapterPath = NULL;
    }

    if (gpcBTAdapterPath) {
        free(gpcBTAdapterPath);
        gpcBTAdapterPath = NULL;
    }

    gfpcBConnectionAuthentication   = NULL;
    gfpcBConnectionIntimation       = NULL;
    gfpcBTransportPathMedia         = NULL;
    gfpcBNegotiateMedia             = NULL;
    gfpcBDevStatusUpdate            = NULL;
    gpcBDevStatusUserData           = NULL;
    gpcBConnAuthUserData            = NULL;
    gpcBConnIntimUserData           = NULL;
    gpcBNegMediaUserData            = NULL;
    gpcBTransPathMediaUserData      = NULL;

    gpcBConnAuthPassKey             = 0;

    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH "'", NULL);
    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_DEVICE_PATH "'", NULL);
    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"",arg0='" BT_DBUS_BLUEZ_ADAPTER_PATH "'", NULL);
    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved'", NULL);
    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='" BT_DBUS_BLUEZ_PATH "',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'", NULL);
    dbus_bus_remove_match(gpDBusConn, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'"",arg0='" BT_DBUS_BLUEZ_PATH "'", NULL);

    dbus_connection_remove_filter(gpDBusConn, btrCore_BTDBusConnectionFilter_cb, NULL);

    gpDBusConn = NULL;

    return 0;
}


char*
BtrCore_BTGetAgentPath (
    void* apBtConn
) {
    char lDefaultBTPath[128] = {'\0'};

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return NULL;

    snprintf(lDefaultBTPath, sizeof(lDefaultBTPath), "/org/bluez/agent_%d", getpid());

    if (gpcBTAgentPath) {
        free(gpcBTAgentPath);
        gpcBTAgentPath = NULL;
    }

    gpcBTAgentPath = strdup(lDefaultBTPath);
    BTRCORELOG_INFO ("\n\nAgent Path: %s", gpcBTAgentPath);
    return gpcBTAgentPath;
}


int
BtrCore_BTReleaseAgentPath (
    void* apBtConn
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (gpcBTAgentPath) {
        free(gpcBTAgentPath);
        gpcBTAgentPath = NULL;
    }

    return 0;
}


int
BtrCore_BTRegisterAgent (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath,
    const char* capabilities
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;

    if (!dbus_connection_register_object_path(gpDBusConn, apBtAgentPath, &gDBusAgentVTable, NULL))  {
        BTRCORELOG_ERROR ("Error registering object path for agent\n");
        return -1;
    }

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             "/org/bluez",
                                             BT_DBUS_BLUEZ_AGENT_MGR_PATH,
                                             "RegisterAgent");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Error allocating new method call\n");
        return -1;
    }

    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_OBJECT_PATH, &apBtAgentPath, DBUS_TYPE_STRING, &capabilities, DBUS_TYPE_INVALID);


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Unable to register agent\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             "/org/bluez",
                                             BT_DBUS_BLUEZ_AGENT_MGR_PATH,
                                             "RequestDefaultAgent");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_OBJECT_PATH, &apBtAgentPath, DBUS_TYPE_INVALID);


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't unregister agent\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;//this was an error case
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTUnregisterAgent (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             "/org/bluez",
                                             BT_DBUS_BLUEZ_AGENT_MGR_PATH,
                                             "UnregisterAgent");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_OBJECT_PATH, &apBtAgentPath, DBUS_TYPE_INVALID);


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Can't unregister agent\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;//this was an error case
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    if (!dbus_connection_unregister_object_path(gpDBusConn, apBtAgentPath)) {
        BTRCORELOG_ERROR ("Error unregistering object path for agent\n");
        return -1;
    }

    return 0;
}


int
BtrCore_BTGetAdapterList (
    void*           apBtConn,
    unsigned int*   apBtNumAdapters,
    char**          apcArrBtAdapterPath
) {
    int         c;
    int         rc = -1;
    int         a = 0;
    int         b = 0;
    int         d = 0;
    int         num = -1;
    char        paths[10][248];
    //char      **paths2 = NULL;

    DBusMessage*    lpDBusReply = NULL;
    DBusMessageIter rootIter;
    bool            adapterFound = FALSE;
    char*           adapter_path;
    char*           dbusObject2;
    char            objectPath[256] = {'\0'};
    char            objectData[256] = {'\0'};

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    lpDBusReply = btrCore_BTSendMethodCall("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("org.bluez.Manager.ListAdapters returned an error\n");
        return rc;
    }

    if (dbus_message_iter_init(lpDBusReply, &rootIter) &&               //point iterator to lpDBusReply message
        DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&rootIter)) { //get the type of message that iter points to

        DBusMessageIter arrayElementIter;
        dbus_message_iter_recurse(&rootIter, &arrayElementIter); //assign new iterator to first element of array

        while (!adapterFound) {
            if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&arrayElementIter)) {

                DBusMessageIter dictEntryIter;
                dbus_message_iter_recurse(&arrayElementIter,&dictEntryIter ); //assign new iterator to first element of (get all dict entries of 1st level (all object paths)

                if (DBUS_TYPE_OBJECT_PATH == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    dbus_message_iter_get_basic(&dictEntryIter, &adapter_path);
                    strcpy(objectPath, adapter_path);
                    ++a;
                }

                dbus_message_iter_next(&dictEntryIter);
                if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    DBusMessageIter innerArrayIter;
                    dbus_message_iter_recurse(&dictEntryIter, &innerArrayIter);

                    while (dbus_message_iter_has_next(&innerArrayIter)) {
                        if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter)) {
                            DBusMessageIter innerDictEntryIter;
                            dbus_message_iter_recurse(&innerArrayIter,&innerDictEntryIter ); //assign new iterator to first element of

                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                char *dbusObject;
                                dbus_message_iter_get_basic(&innerDictEntryIter, &dbusObject);

                                ////// getting all bluetooth adapters object paths //////

                                if (strcmp(dbusObject, BT_DBUS_BLUEZ_ADAPTER_PATH) == 0) {
                                    strcpy(paths[d], adapter_path);
                                    //strcpy(paths2+d,adapter_path);
                                    //paths2[d] = strdup(adapter_path);
                                    //BTRCORELOG_ERROR ("\n\n test");
                                    //(paths2+2) = strdup(adapter_path);
                                    ++d;
                                }
                            }

                            /////// NEW //////////
                            dbus_message_iter_next(&innerDictEntryIter);
                            if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                DBusMessageIter innerArrayIter2;
                                dbus_message_iter_recurse(&innerDictEntryIter, &innerArrayIter2);

                                while (dbus_message_iter_has_next(&innerArrayIter2)) {

                                    if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter2)) {
                                        DBusMessageIter innerDictEntryIter2;
                                        dbus_message_iter_recurse(&innerArrayIter2,&innerDictEntryIter2); //assign new iterator to first element of

                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter2)) {
                                            dbus_message_iter_get_basic(&innerDictEntryIter2, &dbusObject2);
                                        }

                                        ////////////// NEW 2 ////////////
                                        dbus_message_iter_next(&innerDictEntryIter2);
                                        DBusMessageIter innerDictEntryIter3;
                                        char *dbusObject3;

                                        dbus_message_iter_recurse(&innerDictEntryIter2,&innerDictEntryIter3);
                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &dbusObject3);
                                            strcpy(objectData, dbusObject3);
                                            ++b;
                                        }
                                        else if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            bool *device_prop = FALSE;
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &device_prop);
                                        }
                                    }

                                    if (!dbus_message_iter_has_next(&innerArrayIter2)) {
                                        break; //check to see if end of 3rd array
                                    }
                                    else {
                                        dbus_message_iter_next(&innerArrayIter2);
                                    }
                                }
                            }
                        }

                        if (!dbus_message_iter_has_next(&innerArrayIter)) {
                            break; //check to see if end of 2nd array
                        }
                        else {
                            dbus_message_iter_next(&innerArrayIter);
                        }
                    }
                }

                if (!dbus_message_iter_has_next(&arrayElementIter)) {
                    break; //check to see if end of 1st array
                }
                else {
                    dbus_message_iter_next(&arrayElementIter);
                }
            } //while loop end --used to traverse array
        }
    }

    num = d;
    if (apBtNumAdapters && apcArrBtAdapterPath) {
        *apBtNumAdapters = num;

        for (c = 0; c < num; c++) {
            if (*(apcArrBtAdapterPath + c)) {
                BTRCORELOG_DEBUG ("Adapter Path %d is: %s\n", c, paths[c]);
                //strncpy(*(apcArrBtAdapterPath + c), paths[c], BD_NAME_LEN);
                strncpy(apcArrBtAdapterPath[c], paths[c], BD_NAME_LEN);
                rc = 0;
            }
        }
    }

    dbus_message_unref(lpDBusReply);

    return rc;
}


char*
BtrCore_BTGetAdapterPath (
    void*       apBtConn,
    const char* apBtAdapter
) {
    char* defaultAdapter1 = "/org/bluez/hci0";
    char* defaultAdapter2 = "/org/bluez/hci1";
    char* defaultAdapter3 = "/org/bluez/hci2";
    char* bt1 = "hci0";
    char* bt2 = "hci1";
    char* bt3 = "hci2";

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return NULL;

    if (!apBtAdapter)
        return btrCore_BTGetDefaultAdapterPath();


    if (gpcBTAdapterPath) {
        free(gpcBTAdapterPath);
        gpcBTAdapterPath = NULL;
    }

    if (strcmp(apBtAdapter, bt1) == 0) {
        gpcBTAdapterPath = strndup(defaultAdapter1, strlen(defaultAdapter1));
    }

    if (strcmp(apBtAdapter, bt2) == 0) {
        gpcBTAdapterPath = strndup(defaultAdapter2, strlen(defaultAdapter2));
    }

    if (strcmp(apBtAdapter, bt3) == 0) {
        gpcBTAdapterPath = strndup(defaultAdapter3, strlen(defaultAdapter3));
    }


    //BTRCORELOG_ERROR ("\n\nPath is %s: ", gpcBTAdapterPath);
    return gpcBTAdapterPath;
}


int
BtrCore_BTReleaseAdapterPath (
    void*       apBtConn,
    const char* apBtAdapter
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apBtAdapter) {
        return btrCore_BTReleaseDefaultAdapterPath();
    }

    if (gpcBTAdapterPath) {

        if (gpcBTAdapterPath != apBtAdapter) {
            BTRCORELOG_ERROR ("ERROR: Looks like Adapter path has been changed by User\n");
        }

        free(gpcBTAdapterPath);
        gpcBTAdapterPath = NULL;
    }

    return 0;
}


int
BtrCore_BTGetIfceNameVersion (
    void* apBtConn,
    char* apBtOutIfceName,
    char* apBtOutVersion
) {
    FILE*   lfpVersion = NULL;
    char    lcpVersion[8] = {'\0'};

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apBtOutIfceName || !apBtOutVersion)
        return -1;

    lfpVersion = popen("/usr/lib/bluez5/bluetooth/bluetoothd --version", "r");
    if ((lfpVersion == NULL)) {
        BTRCORELOG_ERROR ("Failed to run Version command\n");
        strncpy(lcpVersion, "5.XXX", strlen("5.XXX"));
    }
    else {
        if (fgets(lcpVersion, sizeof(lcpVersion)-1, lfpVersion) == NULL) {
            BTRCORELOG_ERROR ("Failed to Valid Version\n");
            strncpy(lcpVersion, "5.XXX", strlen("5.XXX"));
        }

        pclose(lfpVersion);
    }


    strncpy(apBtOutIfceName, "Bluez", strlen("Bluez"));
    strncpy(apBtOutVersion, lcpVersion, strlen(lcpVersion));
    
    return 0;
}


int
BtrCore_BTGetProp (
    void*               apBtConn,
    const char*         apcBtOpIfcePath,
    enBTOpIfceType      aenBtOpIfceType,
    unBTOpIfceProp      aunBtOpIfceProp,
    void*               apvVal
) {
    int                 rc = 0;

    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusPendingCall*    lpDBusPendC = NULL;
    DBusMessageIter     args;
    DBusMessageIter     arg_i;
    DBusMessageIter     element_i;
    DBusMessageIter     variant_i;
    DBusError           lDBusErr;

    const char*         pParsedKey = NULL;
    const char*         pParsedValueString = NULL;
    int                 parsedValueNumber = 0;
    unsigned int        parsedValueUnsignedNumber = 0;
    unsigned short      parsedValueUnsignedShort = 0;

    const char*         lDBusKey = NULL;
    int                 lDBusType = DBUS_TYPE_INVALID;

    const char*         pInterface          = NULL;
    const char*         pAdapterInterface   = BT_DBUS_BLUEZ_ADAPTER_PATH;
    const char*         pDeviceInterface    = BT_DBUS_BLUEZ_DEVICE_PATH;
    const char*         pMediaTransInterface= BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if ((!apcBtOpIfcePath) || (!apvVal)) {
        BTRCORELOG_ERROR ("enBTRCoreInvalidArg - enBTRCoreInitFailure\n");
        return -1;
    }


    switch (aenBtOpIfceType) {
    case enBTAdapter:
        pInterface = pAdapterInterface;
        switch (aunBtOpIfceProp.enBtAdapterProp) {
        case enBTAdPropName:
            lDBusType = DBUS_TYPE_STRING;
            lDBusKey  = "Alias";
            break;
        case enBTAdPropAddress:
            lDBusType = DBUS_TYPE_STRING;
            lDBusKey  = "Address";
            break;
        case enBTAdPropPowered:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Powered";
            break;
        case enBTAdPropDiscoverable:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Discoverable";
            break;
        case enBTAdPropDiscoverableTimeOut:
            lDBusType = DBUS_TYPE_UINT32;
            lDBusKey  = "DiscoverableTimeout";
            break;
        case enBTAdPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid Adapter Property\n");
            return -1;
        }
        break;
    case enBTDevice:
        pInterface = pDeviceInterface;
        switch (aunBtOpIfceProp.enBtDeviceProp) {
        case enBTDevPropPaired:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Paired";
            break;
        case enBTDevPropConnected:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Connected";
            break;
        case enBTDevPropVendor:
            lDBusType = DBUS_TYPE_UINT16;
            lDBusKey  = "Vendor";
            break;
        case enBTDevPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid Device Property\n");
            return -1;
        }
        break;
    case enBTMediaTransport:
        pInterface = pMediaTransInterface;
        switch (aunBtOpIfceProp.enBtMediaTransportProp) {
        case enBTMedTPropDelay:
            lDBusType = DBUS_TYPE_UINT16;
            lDBusKey  = "Delay";
            break;
        case enBTMedTPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid MediaTransport Property\n");
            return -1;
        }
        break;
    case enBTUnknown:
    default:
        BTRCORELOG_ERROR ("Invalid Operational Interface\n");
        return -1;
    }


    if (!lDBusKey || (lDBusType == DBUS_TYPE_INVALID)) {
        BTRCORELOG_ERROR ("Invalid Interface Property\n");
        return -1;
    }
    

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcBtOpIfcePath,
                                             "org.freedesktop.DBus.Properties",
                                             "GetAll");

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pInterface, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1))
    {
        BTRCORELOG_ERROR ("failed to send message");
    }


    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
    dbus_pending_call_unref(lpDBusPendC);


    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("%s.GetProperties returned an error: '%s'\n", pInterface, lDBusErr.message);
        rc = -1;
        dbus_error_free(&lDBusErr);
    }
    else {
        if (!dbus_message_iter_init(lpDBusReply, &arg_i)) {
            BTRCORELOG_ERROR ("GetProperties lpDBusReply has no arguments.");
            rc = -1;
        }
        else if (dbus_message_iter_get_arg_type(&arg_i) != DBUS_TYPE_ARRAY) {
            BTRCORELOG_ERROR ("GetProperties argument is not an array.");
            rc = -1;
        }
        else {
            dbus_message_iter_recurse(&arg_i, &element_i);
            while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
                if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter dict_i;
                    dbus_message_iter_recurse(&element_i, &dict_i);
                    dbus_message_iter_get_basic(&dict_i, &pParsedKey);

                    if ((pParsedKey) && (strcmp (pParsedKey, lDBusKey) == 0)) {
                        dbus_message_iter_next(&dict_i);
                        dbus_message_iter_recurse(&dict_i, &variant_i);
                        if (lDBusType == DBUS_TYPE_STRING) {
                            dbus_message_iter_get_basic(&variant_i, &pParsedValueString);
                            //BTRCORELOG_ERROR ("Key is %s and the value in string is %s\n", pParsedKey, pParsedValueString);
                            strncpy (apvVal, pParsedValueString, BD_NAME_LEN);
                        }
                        else if (lDBusType == DBUS_TYPE_UINT16) {
                            unsigned short* ptr = (unsigned short*) apvVal;
                            dbus_message_iter_get_basic(&variant_i, &parsedValueUnsignedShort);
                            //BTRCORELOG_ERROR ("Key is %s and the value is %u\n", pParsedKey, parsedValueUnsignedNumber);
                            *ptr = parsedValueUnsignedShort;
                        }
                        else if (lDBusType == DBUS_TYPE_UINT32) {
                            unsigned int* ptr = (unsigned int*) apvVal;
                            dbus_message_iter_get_basic(&variant_i, &parsedValueUnsignedNumber);
                            //BTRCORELOG_ERROR ("Key is %s and the value is %u\n", pParsedKey, parsedValueUnsignedNumber);
                            *ptr = parsedValueUnsignedNumber;
                        }
                        else { /* As of now ints and bools are used. This function has to be extended for array if needed */
                            int* ptr = (int*) apvVal;
                            dbus_message_iter_get_basic(&variant_i, &parsedValueNumber);
                            //BTRCORELOG_ERROR ("Key is %s and the value is %d\n", pParsedKey, parsedValueNumber);
                            *ptr = parsedValueNumber;
                        }
                        rc = 0;
                        break;
                    }
                }

                if (!dbus_message_iter_next(&element_i))
                    break;
            }
        }

        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);

        dbus_message_unref(lpDBusReply);
    }

    return rc;
}


int
BtrCore_BTSetProp (
    void*               apBtConn,
    const char*         apcBtOpIfcePath,
    enBTOpIfceType      aenBtOpIfceType,
    unBTOpIfceProp      aunBtOpIfceProp,
    void*               apvVal
) {
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusMessageIter     lDBusMsgIter;
    DBusMessageIter     lDBusMsgIterValue;
    DBusError           lDBusErr;

    const char*         lDBusTypeAsString;

    const char*         lDBusKey = NULL;
    int                 lDBusType = DBUS_TYPE_INVALID;

    const char*         pInterface          = NULL;
    const char*         pAdapterInterface   = BT_DBUS_BLUEZ_ADAPTER_PATH;
    const char*         pDeviceInterface    = BT_DBUS_BLUEZ_DEVICE_PATH;
    const char*         pMediaTransInterface= BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH;


    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apvVal)
        return -1;


    switch (aenBtOpIfceType) {
    case enBTAdapter:
        pInterface = pAdapterInterface;
        switch (aunBtOpIfceProp.enBtAdapterProp) {
        case enBTAdPropName:
            lDBusType = DBUS_TYPE_STRING;
            lDBusKey  = "Alias";
            break;
        case enBTAdPropAddress:
            lDBusType = DBUS_TYPE_STRING;
            lDBusKey  = "Address";
            break;
        case enBTAdPropPowered:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Powered";
            break;
        case enBTAdPropDiscoverable:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Discoverable";
            break;
        case enBTAdPropDiscoverableTimeOut:
            lDBusType = DBUS_TYPE_UINT32;
            lDBusKey  = "DiscoverableTimeout";
            break;
        case enBTAdPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid Adapter Property\n");
            return -1;
        }
        break;
    case enBTDevice:
        pInterface = pDeviceInterface;
        switch (aunBtOpIfceProp.enBtDeviceProp) {
        case enBTDevPropPaired:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Paired";
            break;
        case enBTDevPropConnected:
            lDBusType = DBUS_TYPE_BOOLEAN;
            lDBusKey  = "Connected";
            break;
        case enBTDevPropVendor:
            lDBusType = DBUS_TYPE_UINT16;
            lDBusKey  = "Vendor";
            break;
        case enBTDevPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid Device Property\n");
            return -1;
        }
        break;
    case enBTMediaTransport:
        pInterface = pMediaTransInterface;
        switch (aunBtOpIfceProp.enBtMediaTransportProp) {
        case enBTMedTPropDelay:
            lDBusType = DBUS_TYPE_UINT16;
            lDBusKey  = "Delay";
            break;
        case enBTMedTPropUnknown:
        default:
            BTRCORELOG_ERROR ("Invalid MediaTransport Property\n");
            return -1;
        }
        break;
    case enBTUnknown:
    default:
        BTRCORELOG_ERROR ("Invalid Operational Interface\n");
        return -1;
    }


    if (!lDBusKey || (lDBusType == DBUS_TYPE_INVALID)) {
       BTRCORELOG_ERROR ("Invalid Interface Property\n");
       return -1;
    }


    switch (lDBusType) {
    case DBUS_TYPE_BOOLEAN:
        lDBusTypeAsString = DBUS_TYPE_BOOLEAN_AS_STRING;
        break;
    case DBUS_TYPE_UINT32:
        lDBusTypeAsString = DBUS_TYPE_UINT32_AS_STRING;
        break;
    case DBUS_TYPE_UINT16:
        lDBusTypeAsString = DBUS_TYPE_UINT16_AS_STRING;
        break;
    case DBUS_TYPE_STRING:
        lDBusTypeAsString = DBUS_TYPE_STRING_AS_STRING;
        break;
    default:
        BTRCORELOG_ERROR ("Invalid DBus Type\n");
        return -1;
    }

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcBtOpIfcePath,
                                             "org.freedesktop.DBus.Properties",
                                             "Set");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }
  
    dbus_message_iter_init_append(lpDBusMsg, &lDBusMsgIter);
    dbus_message_iter_append_basic(&lDBusMsgIter, DBUS_TYPE_STRING, &pInterface);
    dbus_message_iter_append_basic(&lDBusMsgIter, DBUS_TYPE_STRING, &lDBusKey);
    dbus_message_iter_open_container(&lDBusMsgIter, DBUS_TYPE_VARIANT, lDBusTypeAsString, &lDBusMsgIterValue);
    dbus_message_iter_append_basic(&lDBusMsgIterValue, lDBusType, apvVal);
    dbus_message_iter_close_container(&lDBusMsgIter, &lDBusMsgIterValue);
    //dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pInterface, DBUS_TYPE_STRING, &lDBusKey, lDBusType, apvVal, DBUS_TYPE_INVALID);


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTStartDiscovery (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_ADAPTER_PATH,
                                             "StartDiscovery");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTStopDiscovery (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_ADAPTER_PATH,
                                             "StopDiscovery");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTStartLEDiscovery (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;
    DBusMessageIter lDBusMsgIter;
    DBusMessageIter lDBusMsgIterDict, lDBusMsgIterDictStr, lDBusMsgIterVariant;
    char*   lpcKey      = "Transport";
    char*   lpcValue    = "le";
    int     i32DBusType = DBUS_TYPE_STRING;


    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_ADAPTER_PATH,
                                             "SetDiscoveryFilter");
    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }


    dbus_message_iter_init_append(lpDBusMsg, &lDBusMsgIter);
    dbus_message_iter_open_container(&lDBusMsgIter,
                                     DBUS_TYPE_ARRAY,
                                     DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                     DBUS_TYPE_STRING_AS_STRING
                                     DBUS_TYPE_VARIANT_AS_STRING
                                     DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                     &lDBusMsgIterDict);
        dbus_message_iter_open_container(&lDBusMsgIterDict, DBUS_TYPE_DICT_ENTRY, NULL, &lDBusMsgIterDictStr);
            dbus_message_iter_append_basic (&lDBusMsgIterDictStr, DBUS_TYPE_STRING, &lpcKey);
            dbus_message_iter_open_container (&lDBusMsgIterDictStr, DBUS_TYPE_VARIANT, (char *)&i32DBusType, &lDBusMsgIterVariant);
                dbus_message_iter_append_basic (&lDBusMsgIterVariant, i32DBusType, &lpcValue);
            dbus_message_iter_close_container (&lDBusMsgIterDictStr, &lDBusMsgIterVariant);
        dbus_message_iter_close_container (&lDBusMsgIterDict, &lDBusMsgIterDictStr);
    dbus_message_iter_close_container (&lDBusMsgIter, &lDBusMsgIterDict);


    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return BtrCore_BTStartDiscovery(apBtConn, apBtAdapter, apBtAgentPath);
}


int
BtrCore_BTStopLEDiscovery (
    void*       apBtConn,
    const char* apBtAdapter,
    const char* apBtAgentPath
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (BtrCore_BTStopDiscovery(apBtConn, apBtAdapter, apBtAgentPath)) {
        BTRCORELOG_WARN ("Failed to Stop Discovery\n");
    }


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_ADAPTER_PATH,
                                             "SetDiscoveryFilter");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTGetPairedDeviceInfo (
    void*                   apBtConn,
    const char*             apBtAdapter,
    stBTPairedDeviceInfo*   pPairedDeviceInfo
) {
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusMessageIter     rootIter;
    DBusMessageIter     args;
    DBusError           lDBusErr;
    DBusPendingCall*    lpDBusPendC;
    bool                adapterFound = FALSE;


    char*   pdeviceInterface = BT_DBUS_BLUEZ_DEVICE_PATH;
    char*   adapter_path;
    char*   dbusObject2;
    char    paths[32][256];
    char    objectPath[256] = {'\0'};
    char    objectData[256] = {'\0'};
    int     i = 0;
    int     num = 0;
    int     a = 0;
    int     b = 0;
    int     d = 0;

    //char**      paths = NULL;
    stBTDeviceInfo apstBTDeviceInfo;

    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apBtAdapter || !pPairedDeviceInfo)
        return -1;


    memset (pPairedDeviceInfo, 0, sizeof (stBTPairedDeviceInfo));

    dbus_error_init(&lDBusErr);
    lpDBusReply = btrCore_BTSendMethodCall("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("org.bluez.Manager.ListAdapters returned an error: '%s'\n", lDBusErr.message);
        dbus_error_free(&lDBusErr);
    }

    if (dbus_message_iter_init(lpDBusReply, &rootIter) &&               //point iterator to lpDBusReply message
        DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&rootIter)) { //get the type of message that iter points to

        DBusMessageIter arrayElementIter;
        dbus_message_iter_recurse(&rootIter, &arrayElementIter); //assign new iterator to first element of array

        while(!adapterFound) {
            if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&arrayElementIter)) {

                DBusMessageIter dictEntryIter;
                dbus_message_iter_recurse(&arrayElementIter,&dictEntryIter ); //assign new iterator to first element of (get all dict entries of 1st level (all object paths)

                if (DBUS_TYPE_OBJECT_PATH == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    dbus_message_iter_get_basic(&dictEntryIter, &adapter_path);
                    strcpy(objectPath, adapter_path);
                    ++a;
                }

                dbus_message_iter_next(&dictEntryIter);
                if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                    DBusMessageIter innerArrayIter;
                    dbus_message_iter_recurse(&dictEntryIter, &innerArrayIter);

                    while (dbus_message_iter_has_next(&innerArrayIter)) {
                        if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter)) {
                            DBusMessageIter innerDictEntryIter;
                            dbus_message_iter_recurse(&innerArrayIter,&innerDictEntryIter ); //assign new iterator to first element of

                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                char *dbusObject;
                                dbus_message_iter_get_basic(&innerDictEntryIter, &dbusObject);
                            }


                            /////// NEW //////////
                            dbus_message_iter_next(&innerDictEntryIter);
                            if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                DBusMessageIter innerArrayIter2;
                                dbus_message_iter_recurse(&innerDictEntryIter, &innerArrayIter2);

                                while (dbus_message_iter_has_next(&innerArrayIter2)) {
                                    if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter2)) {
                                        DBusMessageIter innerDictEntryIter2;
                                        dbus_message_iter_recurse(&innerArrayIter2,&innerDictEntryIter2); //assign new iterator to first element of

                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter2)) {
                                            dbus_message_iter_get_basic(&innerDictEntryIter2, &dbusObject2);
                                        }


                                        ////////////// NEW 2 ////////////
                                        dbus_message_iter_next(&innerDictEntryIter2);
                                        DBusMessageIter innerDictEntryIter3;
                                        char *dbusObject3;

                                        dbus_message_iter_recurse(&innerDictEntryIter2,&innerDictEntryIter3);
                                        if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &dbusObject3);
                                            strcpy(objectData, dbusObject3);
                                            ++b;
                                        }
                                        else if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                            bool *device_prop = FALSE;
                                            dbus_message_iter_get_basic(&innerDictEntryIter3, &device_prop);

                                            if (strcmp(dbusObject2, "Paired") == 0 && device_prop) {
                                                strcpy(paths[d], adapter_path);
                                                ++d;
                                            }
                                        }
                                    }

                                    if (!dbus_message_iter_has_next(&innerArrayIter2)) {
                                        break;  //check to see if end of 3rd array
                                    }
                                    else {
                                        dbus_message_iter_next(&innerArrayIter2);
                                    }
                                }
                            }
                        }

                        if (!dbus_message_iter_has_next(&innerArrayIter)) {
                            break; //check to see if end of 2nd array
                        }
                        else {
                            dbus_message_iter_next(&innerArrayIter);
                        }
                    }
                }

                if (!dbus_message_iter_has_next(&arrayElementIter)) {
                    break; //check to see if end of 1st array
                }
                else {
                    dbus_message_iter_next(&arrayElementIter);
                }
            } //while loop end --used to traverse array
        }
    }

    num = d;

    /* Update the number of devices */
    pPairedDeviceInfo->numberOfDevices = num;

    /* Update the paths of these devices */
    for ( i = 0; i < num; i++) {
        memset(pPairedDeviceInfo->devicePath[i], '\0', sizeof(pPairedDeviceInfo->devicePath[i]));
        strcpy(pPairedDeviceInfo->devicePath[i], paths[i]);
    }

    dbus_message_unref(lpDBusReply);


    for ( i = 0; i < num; i++) {
        lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                                 pPairedDeviceInfo->devicePath[i],
                                                 "org.freedesktop.DBus.Properties",
                                                 "GetAll");
        dbus_message_iter_init_append(lpDBusMsg, &args);
        dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pdeviceInterface, DBUS_TYPE_INVALID);

        dbus_error_init(&lDBusErr);

        if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
            BTRCORELOG_ERROR ("failed to send message");
            return -1;
        }

        dbus_connection_flush(gpDBusConn);
        dbus_message_unref(lpDBusMsg);
        lpDBusMsg = NULL;

        dbus_pending_call_block(lpDBusPendC);
        lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
        dbus_pending_call_unref(lpDBusPendC);

        if (lpDBusReply != NULL) {
            memset (&apstBTDeviceInfo, 0, sizeof(apstBTDeviceInfo));
            if (0 != btrCore_BTParseDevice(lpDBusReply, &apstBTDeviceInfo)) {
                BTRCORELOG_ERROR ("Parsing the device %s failed..\n", pPairedDeviceInfo->devicePath[i]);
                dbus_message_unref(lpDBusReply);
                return -1;
            }
            else {
                memcpy (&pPairedDeviceInfo->deviceInfo[i], &apstBTDeviceInfo, sizeof(apstBTDeviceInfo));
            }
        }
        dbus_message_unref(lpDBusReply);
    }


    BTRCORELOG_INFO ("Exiting\n");

    return 0;
}


int
BtrCore_BTDiscoverDeviceServices (
    void*                           apBtConn,
    const char*                     apcDevPath,
    stBTDeviceSupportedServiceList* pProfileList
) {
    DBusMessage*        lpDBusMsg       = NULL;
    DBusMessage*        lpDBusReply     = NULL;
    DBusError           lDBusErr;
    DBusMessageIter     args;
    DBusMessageIter     MsgIter;
    DBusPendingCall*    lpDBusPendC;
    int                 match = 0;
    const char*         apcSearchString = "UUIDs";
    const char*         pDeviceInterface= BT_DBUS_BLUEZ_DEVICE_PATH;


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcDevPath,
                                             "org.freedesktop.DBus.Properties",
                                             "GetAll");

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &pDeviceInterface, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
        BTRCORELOG_ERROR ("failed to send message");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);
    lpDBusMsg = NULL;

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
    dbus_pending_call_unref(lpDBusPendC);


    dbus_message_iter_init(lpDBusReply, &MsgIter); //lpDBusMsg is pointer to dbus message received
    //dbus_message_iter_recurse(&MsgIter,&element); //pointer to first element of the dbus messge received
    /*if (!dbus_message_iter_init(lpDBusReply, &MsgIter))
    {
    BTRCORELOG_ERROR ("Message has no arguments!\n");
    }*/

    if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&MsgIter)) {
        DBusMessageIter arrayElementIter;
        dbus_message_iter_recurse(&MsgIter, &arrayElementIter); //assign new iterator to first element of array
        while (dbus_message_iter_has_next(&arrayElementIter)) {
            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&arrayElementIter)) {
                char *dbusObject2;
                dbus_message_iter_get_basic(&arrayElementIter, &dbusObject2);
                if (strcmp(apcSearchString, dbusObject2) == 0) {
                    match = 1;
                }
                else {
                    match = 0;
                }
            }

            if (!dbus_message_iter_has_next(&arrayElementIter)) {
                break; //check to see if end of 3rd array
            }
            else {
                dbus_message_iter_next(&arrayElementIter);
            }
        }
    }

    return match;
}


int
BtrCore_BTFindServiceSupported (
    void*           apBtConn,
    const char*     apcDevPath,
    const char*     apcSearchString,
    char*           apcDataString
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;
    DBusMessageIter arg_i, element_i;
    DBusMessageIter dict_i;
    int dbus_type;

    int match;
    const char* value;
    char* ret;

    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apcDevPath)
        return -1;

   //BTRCORELOG_ERROR ("%d\t: %s - apcDevPath is %s\n and service UUID is %s", __LINE__, __FUNCTION__, apcDevPath, apcSearchString);
    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apcDevPath,
                                             BT_DBUS_BLUEZ_DEVICE_PATH,
                                             "DiscoverServices");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    match = 0; //assume it does not match
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &apcSearchString, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("Failure attempting to Discover Services\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    if (!dbus_message_iter_init(lpDBusReply, &arg_i)) {
       BTRCORELOG_ERROR ("DiscoverServices lpDBusReply has no information.");
       return -1;
    }

    dbus_type = dbus_message_iter_get_arg_type(&arg_i);
    // BTRCORELOG_ERROR ("type is %d\n", dbus_type);

    dbus_message_iter_recurse(&arg_i, &element_i);
    dbus_type = dbus_message_iter_get_arg_type(&element_i);
    //BTRCORELOG_ERROR ("checking the type, it is %d\n",dbus_type);

    while (dbus_message_iter_get_arg_type(&element_i) != DBUS_TYPE_INVALID) {
        dbus_type = dbus_message_iter_get_arg_type(&element_i);
        //BTRCORELOG_ERROR ("next element_i type is %d\n",dbus_type);

        if (dbus_message_iter_get_arg_type(&element_i) == DBUS_TYPE_DICT_ENTRY) {

            dbus_message_iter_recurse(&element_i, &dict_i);
            dbus_type = dbus_message_iter_get_arg_type(&dict_i);
            // BTRCORELOG_ERROR ("checking the dict subtype, it is %d\n",dbus_type);

            dbus_message_iter_next(&dict_i);
            dbus_type = dbus_message_iter_get_arg_type(&dict_i);
            // BTRCORELOG_ERROR ("interating the dict subtype, it is %d\n",dbus_type);
            dbus_message_iter_get_basic(&dict_i, &value);

            // BTRCORELOG_ERROR ("Services: %s\n",value);
            if (apcDataString != NULL) {
                strcpy(apcDataString, value);
            }

            // lets strstr to see if "uuid value="<UUID>" is there
            ret =  strstr(value, apcSearchString);
            if (ret !=NULL) {
                match = 1;//assume it does match
                // BTRCORELOG_ERROR ("match\n");
            }
            else {
                //BTRCORELOG_ERROR ("NO match\n");
                match = 0;//assume it does not match
            }
        }

        //load the new device into our list of scanned devices
        if (!dbus_message_iter_next(&element_i))
            break;

    }

    (void)dbus_type;

    return match;
}


int
BtrCore_BTPerformAdapterOp (
    void*           apBtConn,
    const char*     apBtAdapter,
    const char*     apBtAgentPath,
    const char*     apcDevPath,
    enBTAdapterOp   aenBTAdpOp
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;
    DBusMessageIter rootIter;
    bool            adapterFound = FALSE;
    char*           adapter_path = NULL;
    char            deviceObjectPath[256] = {'\0'};
    char            deviceOpString[64] = {'\0'};
    char            objectPath[256] = {'\0'};
    char            objectData[256] = {'\0'};
    int             rc = 0;
    int             a = 0;
    int             b = 0;

    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apBtAdapter || !apBtAgentPath || !apcDevPath || (aenBTAdpOp == enBTAdpOpUnknown))
        return -1;


    switch (aenBTAdpOp) {
        case enBTAdpOpFindPairedDev:
        strcpy(deviceOpString, "FindDevice");
        break;
        case enBTAdpOpCreatePairedDev:
        strcpy(deviceOpString, "Pair");
        break;
        case enBTAdpOpRemovePairedDev:
        strcpy(deviceOpString, "RemoveDevice");
        break;
        case enBTAdpOpUnknown:
        default:
        rc = -1;
        break;
    }

    if (rc == -1)
        return rc;


    if (aenBTAdpOp == enBTAdpOpFindPairedDev) {
        lpDBusReply = btrCore_BTSendMethodCall("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");

        if (lpDBusReply && 
            dbus_message_iter_init(lpDBusReply, &rootIter) &&               //point iterator to lpDBusReply message
            DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&rootIter)) { //get the type of message that iter points to

            DBusMessageIter arrayElementIter;
            dbus_message_iter_recurse(&rootIter, &arrayElementIter); //assign new iterator to first element of array

            while(!adapterFound) {
                if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&arrayElementIter)) {

                    DBusMessageIter dictEntryIter;
                    dbus_message_iter_recurse(&arrayElementIter,&dictEntryIter ); //assign new iterator to first element of (get all dict entries of 1st level (all object paths)

                    if (DBUS_TYPE_OBJECT_PATH == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                        dbus_message_iter_get_basic(&dictEntryIter, &adapter_path);
                        strcpy(objectPath, adapter_path);
                        ++a;
                    }

                    dbus_message_iter_next(&dictEntryIter);
                    if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                        DBusMessageIter innerArrayIter;
                        dbus_message_iter_recurse(&dictEntryIter, &innerArrayIter);

                        while (dbus_message_iter_has_next(&innerArrayIter)) {
                            if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter)) {
                                DBusMessageIter innerDictEntryIter;
                                dbus_message_iter_recurse(&innerArrayIter,&innerDictEntryIter ); //assign new iterator to first element of

                                if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                    char *dbusObject;
                                    dbus_message_iter_get_basic(&innerDictEntryIter, &dbusObject);
                                    ////// getting default adapter path //////
                                }


                                /////// NEW //////////
                                dbus_message_iter_next(&innerDictEntryIter);
                                if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                    DBusMessageIter innerArrayIter2;
                                    dbus_message_iter_recurse(&innerDictEntryIter, &innerArrayIter2);

                                    while (dbus_message_iter_has_next(&innerArrayIter2)) {
                                        if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter2)) {
                                            DBusMessageIter innerDictEntryIter2;
                                            dbus_message_iter_recurse(&innerArrayIter2,&innerDictEntryIter2); //assign new iterator to first element of
                                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter2)) {
                                                char *dbusObject2;
                                                dbus_message_iter_get_basic(&innerDictEntryIter2, &dbusObject2);
                                            }


                                            ////////////// NEW 2 ////////////
                                            dbus_message_iter_next(&innerDictEntryIter2);
                                            DBusMessageIter innerDictEntryIter3;
                                            char *dbusObject3;

                                            dbus_message_iter_recurse(&innerDictEntryIter2,&innerDictEntryIter3);
                                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                                dbus_message_iter_get_basic(&innerDictEntryIter3, &dbusObject3);
                                                strcpy(objectData, dbusObject3);

                                                if (strcmp(apcDevPath, objectData) == 0) {
                                                    ++b;
                                                    adapterFound = TRUE;
                                                    break;
                                                }
                                            }
                                            else if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                                bool *device_prop = FALSE;
                                                dbus_message_iter_get_basic(&innerDictEntryIter3, &device_prop);
                                            }
                                        }

                                        if (!dbus_message_iter_has_next(&innerArrayIter2)) {
                                            break; //check to see if end of 3rd array
                                        }
                                        else {
                                            dbus_message_iter_next(&innerArrayIter2);
                                        }
                                    }
                                }
                            }

                            if (!dbus_message_iter_has_next(&innerArrayIter)) {
                                break; //check to see if end of 2nd array
                            }
                            else {
                                dbus_message_iter_next(&innerArrayIter);
                            }
                        }
                    }

                    if (!dbus_message_iter_has_next(&arrayElementIter)) {
                        break; //check to see if end of 1st array
                    }
                    else {
                        dbus_message_iter_next(&arrayElementIter);
                    }
                } //while loop end --used to traverse arra
            }

            dbus_message_unref(lpDBusReply);
        }
    }

    else if (aenBTAdpOp == enBTAdpOpRemovePairedDev) {
        lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                                 apBtAdapter,
                                                 BT_DBUS_BLUEZ_ADAPTER_PATH,
                                                 deviceOpString);
        if (!lpDBusMsg) {
            BTRCORELOG_ERROR ("Can't allocate new method call\n");
            return -1;
        }

        dbus_message_append_args(lpDBusMsg, DBUS_TYPE_OBJECT_PATH, &apcDevPath, DBUS_TYPE_INVALID);

        dbus_error_init(&lDBusErr);
        lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
        dbus_message_unref(lpDBusMsg);

        if (!lpDBusReply) {
            BTRCORELOG_ERROR ("UnPairing failed...\n");
            btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
            return -1;
        }

        dbus_message_unref(lpDBusReply);
    }

    else if (aenBTAdpOp == enBTAdpOpCreatePairedDev) {
        lpDBusReply = btrCore_BTSendMethodCall("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");

        if (lpDBusReply &&
            dbus_message_iter_init(lpDBusReply, &rootIter) &&               //point iterator to lpDBusReply message
            DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&rootIter)) { //get the type of message that iter points to

            DBusMessageIter arrayElementIter;
            dbus_message_iter_recurse(&rootIter, &arrayElementIter); //assign new iterator to first element of array

            while (!adapterFound) {
                if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&arrayElementIter)) {

                    DBusMessageIter dictEntryIter;
                    dbus_message_iter_recurse(&arrayElementIter,&dictEntryIter ); //assign new iterator to first element of (get all dict entries of 1st level (all object paths)

                    if (DBUS_TYPE_OBJECT_PATH == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                        dbus_message_iter_get_basic(&dictEntryIter, &adapter_path);
                        strcpy(objectPath, adapter_path);
                        ++a;
                    }

                    dbus_message_iter_next(&dictEntryIter);
                    if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&dictEntryIter)) {
                        DBusMessageIter innerArrayIter;
                        dbus_message_iter_recurse(&dictEntryIter, &innerArrayIter);

                        while (dbus_message_iter_has_next(&innerArrayIter)) {
                            if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter)) {
                                DBusMessageIter innerDictEntryIter;
                                dbus_message_iter_recurse(&innerArrayIter,&innerDictEntryIter ); //assign new iterator to first element of

                                if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                    char *dbusObject;
                                    dbus_message_iter_get_basic(&innerDictEntryIter, &dbusObject);
                                    ////// getting default adapter path //////
                                }

                                /////// NEW //////////
                                dbus_message_iter_next(&innerDictEntryIter);
                                if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&innerDictEntryIter)) {
                                    DBusMessageIter innerArrayIter2;
                                    dbus_message_iter_recurse(&innerDictEntryIter, &innerArrayIter2);

                                    while (dbus_message_iter_has_next(&innerArrayIter2)) {
                                        if (DBUS_TYPE_DICT_ENTRY == dbus_message_iter_get_arg_type(&innerArrayIter2)) {
                                            DBusMessageIter innerDictEntryIter2;
                                            dbus_message_iter_recurse(&innerArrayIter2,&innerDictEntryIter2); //assign new iterator to first element of

                                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter2)) {
                                                char *dbusObject2;
                                                dbus_message_iter_get_basic(&innerDictEntryIter2, &dbusObject2);
                                            }

                                            ////////////// NEW 2 ////////////
                                            dbus_message_iter_next(&innerDictEntryIter2);
                                            DBusMessageIter innerDictEntryIter3;
                                            char *dbusObject3;

                                            dbus_message_iter_recurse(&innerDictEntryIter2,&innerDictEntryIter3);
                                            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                                dbus_message_iter_get_basic(&innerDictEntryIter3, &dbusObject3);
                                                strcpy(objectData, dbusObject3);
                                                if (strcmp(apcDevPath, objectData) == 0) {
                                                    ++b;
                                                    strcpy(deviceObjectPath,adapter_path);
                                                    adapterFound = TRUE;
                                                    break;
                                                }
                                            }
                                            else if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&innerDictEntryIter3)) {
                                                bool *device_prop = FALSE;
                                                dbus_message_iter_get_basic(&innerDictEntryIter3, &device_prop);
                                            }
                                        }

                                        if (!dbus_message_iter_has_next(&innerArrayIter2)) {
                                            break; //check to see if end of 3rd array
                                        }
                                        else {
                                            dbus_message_iter_next(&innerArrayIter2);
                                        }
                                    }
                                }
                            }

                            if (!dbus_message_iter_has_next(&innerArrayIter)) {
                                break; //check to see if end of 2nd array
                            }
                            else {
                                dbus_message_iter_next(&innerArrayIter);
                            }
                        }
                    }

                    if (!dbus_message_iter_has_next(&arrayElementIter)) {
                        break; //check to see if end of 1st array
                    }
                    else {
                        dbus_message_iter_next(&arrayElementIter);
                    }
                }   //while loop end --used to traverse arra
            }

            dbus_message_unref(lpDBusReply);
        }



        lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                                 deviceObjectPath,
                                                 BT_DBUS_BLUEZ_DEVICE_PATH,
                                                 deviceOpString);
        if (!lpDBusMsg) {
            BTRCORELOG_ERROR ("Can't allocate new method call\n");
            return -1;
        }

        dbus_error_init(&lDBusErr);
        lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
        dbus_message_unref(lpDBusMsg);

        if (!lpDBusReply) {
            BTRCORELOG_ERROR ("Pairing failed...\n");
            btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
            return -1;
        }

        dbus_message_unref(lpDBusReply);
    }

    return 0;
}


int
BtrCore_BTIsDeviceConnectable (
    void*       apBtConn,
    const char* apcDevPath
) {
    FILE*   lfpL2Ping = NULL;
    int     i32OpRet = -1;
    char    lcpL2PingIp[64] = {'\0'};
    char    lcpL2PingOp[512] = {'\0'};

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apcDevPath)
        return -1;

    snprintf(lcpL2PingIp, 128, "l2ping -i hci0 -c 3 -s 2 -d 2 %s", apcDevPath);
    BTRCORELOG_INFO ("lcpL2PingIp: %s\n", lcpL2PingIp);

    lfpL2Ping = popen(lcpL2PingIp, "r");
    if ((lfpL2Ping == NULL)) {
        BTRCORELOG_ERROR ("Failed to run BTIsDeviceConnectable command\n");
    }
    else {
        if (fgets(lcpL2PingOp, sizeof(lcpL2PingOp)-1, lfpL2Ping) == NULL) {
            BTRCORELOG_ERROR ("Failed to Output of l2ping\n");
        }
        else {
            BTRCORELOG_WARN ("Output of l2ping =  %s\n", lcpL2PingOp);
            if (!strstr(lcpL2PingOp, "Host is down")) {
                i32OpRet = 0;
            }
        }

        pclose(lfpL2Ping);
    }

    return i32OpRet;
}


int
BtrCore_BTConnectDevice (
    void*           apBtConn,
    const char*     apDevPath,
    enBTDeviceType  aenBTDeviceType
) {
    DBusMessage*     lpDBusMsg    = NULL;
    DBusPendingCall* pending_call = NULL;
    DBusError        dbus_error;
    dbus_bool_t      lDBusOp;


    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apDevPath)
        return -1;


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apDevPath,
                                             BT_DBUS_BLUEZ_DEVICE_PATH,
                                             "Connect");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_error_init(&dbus_error);
    lDBusOp = dbus_connection_send_with_reply(gpDBusConn,
                                              lpDBusMsg,
                                              &pending_call,
                                              DBUS_TIMEOUT_USE_DEFAULT);

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    lDBusOp = dbus_pending_call_set_notify(pending_call,
                                           btrCore_BTConnectDeviceConfirmation_cb,
                                           gpDBusConn, NULL);

    if (!lDBusOp) {
        dbus_pending_call_cancel(pending_call);
        dbus_pending_call_unref(pending_call);
        return -1;
    }

    return 0;
}


int
BtrCore_BTDisconnectDevice (
    void*           apBtConn,
    const char*     apDevPath,
    enBTDeviceType  aenBTDeviceType
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;


    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apDevPath)
        return -1;

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apDevPath,
                                             BT_DBUS_BLUEZ_DEVICE_PATH,
                                             "Disconnect");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTRegisterMedia (
    void*           apBtConn,
    const char*     apBtAdapter,
    enBTDeviceType  aenBTDevType,
    void*           apBtUUID,
    void*           apBtMediaCodec,
    void*           apBtMediaCapabilities,
    int             apBtMediaCapabilitiesSize,
    int             abBtMediaDelayReportEnable
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;
    DBusMessageIter lDBusMsgIter;
    DBusMessageIter lDBusMsgIterArr;
    dbus_bool_t     lDBusOp;
    dbus_bool_t     lBtMediaDelayReport = FALSE;

    const char*     lpBtMediaType;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    switch (aenBTDevType) {
    case enBTDevAudioSink:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT;
        break;
    case enBTDevAudioSource:
        lpBtMediaType = BT_MEDIA_A2DP_SINK_ENDPOINT;
        break;
    case enBTDevHFPHeadset:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT; //TODO: Check if this is correct
        break;
    case enBTDevHFPHeadsetGateway:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT; //TODO: Check if this is correct
        break;
    case enBTDevUnknown:
    default:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT;
        break;
    }

    if (abBtMediaDelayReportEnable)
        lBtMediaDelayReport = TRUE;

    lDBusOp = dbus_connection_register_object_path(gpDBusConn, lpBtMediaType, &gDBusMediaEndpointVTable, NULL);
    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Can't Register Media Object\n");
        return -1;
    }

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_MEDIA_PATH,
                                             "RegisterEndpoint");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_iter_init_append (lpDBusMsg, &lDBusMsgIter);
    dbus_message_iter_append_basic (&lDBusMsgIter, DBUS_TYPE_OBJECT_PATH, &lpBtMediaType);
    dbus_message_iter_open_container (&lDBusMsgIter, DBUS_TYPE_ARRAY, "{sv}", &lDBusMsgIterArr);
    {
        DBusMessageIter lDBusMsgIterDict, lDBusMsgIterVariant;
        char*   key = "UUID";
        int     type = DBUS_TYPE_STRING;

        dbus_message_iter_open_container (&lDBusMsgIterArr, DBUS_TYPE_DICT_ENTRY, NULL, &lDBusMsgIterDict);
            dbus_message_iter_append_basic (&lDBusMsgIterDict, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container (&lDBusMsgIterDict, DBUS_TYPE_VARIANT, (char *)&type, &lDBusMsgIterVariant);
                dbus_message_iter_append_basic (&lDBusMsgIterVariant, type, &apBtUUID);
            dbus_message_iter_close_container (&lDBusMsgIterDict, &lDBusMsgIterVariant);
        dbus_message_iter_close_container (&lDBusMsgIterArr, &lDBusMsgIterDict);
    }
    {
        DBusMessageIter lDBusMsgIterDict, lDBusMsgIterVariant;
        char*   key = "Codec";
        int     type = DBUS_TYPE_BYTE;

        dbus_message_iter_open_container (&lDBusMsgIterArr, DBUS_TYPE_DICT_ENTRY, NULL, &lDBusMsgIterDict);
            dbus_message_iter_append_basic (&lDBusMsgIterDict, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container (&lDBusMsgIterDict, DBUS_TYPE_VARIANT, (char *)&type, &lDBusMsgIterVariant);
                dbus_message_iter_append_basic (&lDBusMsgIterVariant, type, &apBtMediaCodec);
            dbus_message_iter_close_container (&lDBusMsgIterDict, &lDBusMsgIterVariant);
        dbus_message_iter_close_container (&lDBusMsgIterArr, &lDBusMsgIterDict);
    }
    {
        DBusMessageIter lDBusMsgIterDict, lDBusMsgIterVariant;
        char*   key = "DelayReporting";
        int     type = DBUS_TYPE_BOOLEAN;

        dbus_message_iter_open_container (&lDBusMsgIterArr, DBUS_TYPE_DICT_ENTRY, NULL, &lDBusMsgIterDict);
            dbus_message_iter_append_basic (&lDBusMsgIterDict, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container (&lDBusMsgIterDict, DBUS_TYPE_VARIANT, (char *)&type, &lDBusMsgIterVariant);
                dbus_message_iter_append_basic (&lDBusMsgIterVariant, type, &lBtMediaDelayReport);
            dbus_message_iter_close_container (&lDBusMsgIterDict, &lDBusMsgIterVariant);
        dbus_message_iter_close_container (&lDBusMsgIterArr, &lDBusMsgIterDict);
    }
    {
        DBusMessageIter lDBusMsgIterDict, lDBusMsgIterVariant, lDBusMsgIterSubArray;
        char*   key = "Capabilities";
        int     type = DBUS_TYPE_BYTE;

        char array_type[5] = "a";
        strncat (array_type, (char*)&type, sizeof(array_type));

        dbus_message_iter_open_container (&lDBusMsgIterArr, DBUS_TYPE_DICT_ENTRY, NULL, &lDBusMsgIterDict);
            dbus_message_iter_append_basic (&lDBusMsgIterDict, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container (&lDBusMsgIterDict, DBUS_TYPE_VARIANT, array_type, &lDBusMsgIterVariant);
                dbus_message_iter_open_container (&lDBusMsgIterVariant, DBUS_TYPE_ARRAY, (char *)&type, &lDBusMsgIterSubArray);
                    dbus_message_iter_append_fixed_array (&lDBusMsgIterSubArray, type, &apBtMediaCapabilities, apBtMediaCapabilitiesSize);
                dbus_message_iter_close_container (&lDBusMsgIterVariant, &lDBusMsgIterSubArray);
            dbus_message_iter_close_container (&lDBusMsgIterDict, &lDBusMsgIterVariant);
        dbus_message_iter_close_container (&lDBusMsgIterArr, &lDBusMsgIterDict);
    }
    dbus_message_iter_close_container (&lDBusMsgIter, &lDBusMsgIterArr);


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block (gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTUnRegisterMedia (
    void*           apBtConn,
    const char*     apBtAdapter,
    enBTDeviceType  aenBTDevType
) {
    DBusMessage*    lpDBusMsg   = NULL;
    dbus_bool_t     lDBusOp;

    const char*      lpBtMediaType;

    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    switch (aenBTDevType) {
    case enBTDevAudioSink:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT;
        break;
    case enBTDevAudioSource:
        lpBtMediaType = BT_MEDIA_A2DP_SINK_ENDPOINT;
        break;
    case enBTDevHFPHeadset:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT; //TODO: Check if this is correct
        break;
    case enBTDevHFPHeadsetGateway:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT; //TODO: Check if this is correct
        break;
    case enBTDevUnknown:
    default:
        lpBtMediaType = BT_MEDIA_A2DP_SOURCE_ENDPOINT;
        break;
    }


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtAdapter,
                                             BT_DBUS_BLUEZ_MEDIA_PATH,
                                             "UnregisterEndpoint");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_OBJECT_PATH, &lpBtMediaType, DBUS_TYPE_INVALID);

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }

    lDBusOp = dbus_connection_unregister_object_path(gpDBusConn, lpBtMediaType);
    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Can't Register Media Object\n");
        return -1;
    }


    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTAcquireDevDataPath (
    void*   apBtConn,
    char*   apcDevTransportPath,
    int*    dataPathFd,
    int*    dataReadMTU,
    int*    dataWriteMTU
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;
    dbus_bool_t     lDBusOp;

    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apcDevTransportPath)
        return -1;

    lpDBusMsg = dbus_message_new_method_call (BT_DBUS_BLUEZ_PATH,
                                              apcDevTransportPath,
                                              BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH,
                                              "Acquire");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block (gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    lDBusOp = dbus_message_get_args(lpDBusReply, &lDBusErr,
                                    DBUS_TYPE_UNIX_FD, dataPathFd,
                                    DBUS_TYPE_UINT16,  dataReadMTU,
                                    DBUS_TYPE_UINT16,  dataWriteMTU,
                                    DBUS_TYPE_INVALID);
    dbus_message_unref(lpDBusReply);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Can't get lpDBusReply arguments\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTReleaseDevDataPath (
    void*   apBtConn,
    char*   apcDevTransportPath
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;


    if (!gpDBusConn || (gpDBusConn != apBtConn) || !apcDevTransportPath)
        return -1;

    lpDBusMsg = dbus_message_new_method_call (BT_DBUS_BLUEZ_PATH,
                                              apcDevTransportPath,
                                              BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH,
                                              "Release");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }


    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block (gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    return 0;
}


int
BtrCore_BTSendReceiveMessages (
    void*   apBtConn
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if(dbus_connection_read_write_dispatch(gpDBusConn, 25) != TRUE) {
        return -1;
    }

    return 0;
}


int
BtrCore_BTRegisterDevStatusUpdatecB (
    void*                               apBtConn,
    fPtr_BtrCore_BTDevStatusUpdate_cB   afpcBDevStatusUpdate,
    void*                               apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!afpcBDevStatusUpdate)
        return -1;

    gfpcBDevStatusUpdate    = afpcBDevStatusUpdate;
    gpcBDevStatusUserData   = apUserData;

    return 0;
}


int
BtrCore_BTRegisterMediaStatusUpdatecB (
    void*                                 apBtConn,
    fPtr_BtrCore_BTMediaStatusUpdate_cB   afpcBMediaStatusUpdate,
    void*                                 apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!afpcBMediaStatusUpdate)
        return -1;

    gfpcBMediaStatusUpdate    = afpcBMediaStatusUpdate;
    gpcBMediaStatusUserData   = apUserData;

    return 0;
}


int
BtrCore_BTRegisterConnIntimationcB (
    void*                       apBtConn,
    fPtr_BtrCore_BTConnIntim_cB afpcBConnIntim,
    void*                       apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!afpcBConnIntim)
        return -1;

    gfpcBConnectionIntimation = afpcBConnIntim;
    gpcBConnIntimUserData = apUserData;

    return 0;
}


int
BtrCore_BTRegisterConnAuthcB (
    void*                       apBtConn,
    fPtr_BtrCore_BTConnAuth_cB  afpcBConnAuth,
    void*                       apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!afpcBConnAuth)
        return -1;

    gfpcBConnectionAuthentication = afpcBConnAuth;
    gpcBConnAuthUserData = apUserData;

    return 0;
}


int
BtrCore_BTRegisterNegotiateMediacB (
    void*                               apBtConn,
    const char*                         apBtAdapter,
    fPtr_BtrCore_BTNegotiateMedia_cB    afpcBNegotiateMedia,
    void*                               apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apBtAdapter || !afpcBNegotiateMedia)
        return -1;

    gfpcBNegotiateMedia = afpcBNegotiateMedia;
    gpcBNegMediaUserData = apUserData;

    return 0;
}


int
BtrCore_BTRegisterTransportPathMediacB (
    void*                                   apBtConn,
    const char*                             apBtAdapter,
    fPtr_BtrCore_BTTransportPathMedia_cB    afpcBTransportPathMedia,
    void*                                   apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apBtAdapter || !afpcBTransportPathMedia)
        return -1;

    gfpcBTransportPathMedia = afpcBTransportPathMedia;
    gpcBTransPathMediaUserData = apUserData;

    return 0;
}


int
BtrCore_BTRegisterMediaPlayerPathcB (
    void*                                   apBtConn,
    const char*                             apBtAdapter,
    fPtr_BtrCore_BTMediaPlayerPath_cB       afpcBTMediaPlayerPath,
    void*                                   apUserData
) {
    if (!gpDBusConn || (gpDBusConn != apBtConn))
        return -1;

    if (!apBtAdapter || !afpcBTMediaPlayerPath)
        return -1;

    gfpcBTMediaPlayerPath = afpcBTMediaPlayerPath;
    gpcBMediaPlayerPathUserData = apUserData;

    return 0;
}

/////////////////////////////////////////////////////         AVRCP Functions         ////////////////////////////////////////////////////
/* Get Player Object Path on Remote BT Device*/
char*
BtrCore_BTGetMediaPlayerPath (
    void*          apBtConn,
    const char*    apBtDevPath
) {
    char*          playerObjectPath = NULL;
    bool           isConnected      = FALSE;


    if (!gpDBusConn || (gpDBusConn != apBtConn)) {
       BTRCORELOG_ERROR ("DBus Connection Failure!!!");
       return NULL;
    }

    if (btrCore_BTGetInterfaceProperty(apBtConn, apBtDevPath, BT_DBUS_BLUEZ_MEDIA_CTRL_PATH, "Connected", (void*)&isConnected)) {
       BTRCORELOG_ERROR ("Failed to get %s property : Connected!!!", BT_DBUS_BLUEZ_MEDIA_CTRL_PATH);
       return NULL;
    }
    if (FALSE == isConnected) {
       BTRCORELOG_WARN ("%s is not connected", BT_DBUS_BLUEZ_MEDIA_CTRL_PATH);
       return NULL;
    }

    if (btrCore_BTGetInterfaceProperty(apBtConn, apBtDevPath, BT_DBUS_BLUEZ_MEDIA_CTRL_PATH, "Player", (void*)&playerObjectPath)) {
       BTRCORELOG_ERROR ("Failed to get %s property : Player!!!", BT_DBUS_BLUEZ_MEDIA_CTRL_PATH);
       return NULL;
    }

    return playerObjectPath;
}



/* Control Media on Remote BT Device*/
int
BtrCore_BTDevMediaControl (
    void*             apBtConn,
    const char*       apMediaPlayerPath,
    enBTMediaControl  aenBTMediaOper
) {
    dbus_bool_t      lDBusOp;
    DBusMessage*     lpDBusMsg      = NULL;
    char             mediaOper[16]  = "\0";

    if (!gpDBusConn || (gpDBusConn != apBtConn)) {
       BTRCORELOG_ERROR ("DBus Connection Failure!!!");
       return -1;
    }

    switch (aenBTMediaOper) {
    case enBTMediaCtrlPlay:
        strcpy(mediaOper, "Play");
        break;
    case enBTMediaCtrlPause:
        strcpy(mediaOper, "Pause");
        break;
    case enBTMediaCtrlStop:
        strcpy(mediaOper, "Stop");
        break;
    case enBTMediaCtrlNext:
        strcpy(mediaOper, "Next");
        break;
    case enBTMediaCtrlPrevious:
        strcpy(mediaOper, "Previous");
        break;
    case enBTMediaCtrlFastForward:
        strcpy(mediaOper, "FastForward");
        break;
    case enBTMediaCtrlRewind:
        strcpy(mediaOper, "Rewind");
        break;
    case enBTMediaCtrlVolumeUp:
        strcpy(mediaOper, "VolumeUp");
        break;
    case enBTMediaCtrlVolumeDown:
        strcpy(mediaOper, "VolumeDown");
        break;
    }


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apMediaPlayerPath,
                                             BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH,
                                             mediaOper);

    if (lpDBusMsg == NULL) {
       BTRCORELOG_ERROR ("Can't allocate new method call\n");
       return -1;
    }

    lDBusOp = dbus_connection_send(gpDBusConn, lpDBusMsg, NULL);
    dbus_message_unref(lpDBusMsg);

    if (!lDBusOp) {
        BTRCORELOG_ERROR ("Not enough memory for message send\n");
        return -1;
    }
    dbus_connection_flush(gpDBusConn);

    return 0;
}

int
BtrCore_BTGetTransportState (
    void*             apBtConn,
    const char*       apBtDataPath,
    void*             pState
) { 
  /* switch() */
  return btrCore_BTGetInterfaceProperty (apBtConn, apBtDataPath, BT_DBUS_BLUEZ_MEDIA_TRANSPORT_PATH, "State", pState);
}

/* Get Media Player Property on Remote BT Device*/
int
BtrCore_BTGetMediaPlayerProperty (
    void*             apBtConn,
    const char*       apBtMediaPlayerPath,
    const char*       mediaProperty,
    void*             mediaPropertyValue
) {
  /* switch() */
  return btrCore_BTGetInterfaceProperty (apBtConn, apBtMediaPlayerPath, BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH, mediaProperty, mediaPropertyValue);
}


int 
btrCore_BTGetInterfaceProperty (
    void*             apBtConn,
    const char*       apBtObjectPath,
    const char*       apBtInterfacePath,
    const char*       property,
    void*             propertyValue
) {
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusPendingCall*    lpDBusPendC = NULL;
    DBusError           lDBusErr;
    DBusMessageIter     args;

    if (!gpDBusConn || (gpDBusConn != apBtConn)) {
       BTRCORELOG_ERROR ("DBus Connection Failure %p %p!!!", gpDBusConn, apBtConn);
       return -1;
    }

  
    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtObjectPath,
                                             "org.freedesktop.DBus.Properties",
                                             "Get");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &apBtInterfacePath, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
        BTRCORELOG_ERROR ("failed to send message");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);
    dbus_pending_call_unref(lpDBusPendC);

    DBusMessageIter MsgIter, element;
    int dbus_type = DBUS_TYPE_INVALID;

    dbus_message_iter_init(lpDBusReply, &MsgIter);            // lpDBusMsg is pointer to dbus message received
    dbus_message_iter_recurse(&MsgIter, &element);            // pointer to first element of the dbus messge received
    dbus_type = dbus_message_iter_get_arg_type(&element);

    if (DBUS_TYPE_STRING      == dbus_type ||
        DBUS_TYPE_UINT32      == dbus_type ||
        DBUS_TYPE_BOOLEAN     == dbus_type ||
        DBUS_TYPE_OBJECT_PATH == dbus_type ||
        DBUS_TYPE_UINT16      == dbus_type ||
        DBUS_TYPE_UINT64      == dbus_type ||
        DBUS_TYPE_INT16       == dbus_type ||
        DBUS_TYPE_INT32       == dbus_type ||
        DBUS_TYPE_INT64       == dbus_type ||
        DBUS_TYPE_BYTE        == dbus_type ||
        DBUS_TYPE_DOUBLE      == dbus_type ){
          dbus_message_iter_get_basic(&element, propertyValue);
    }

    return 0;
}


/* Set Media Property on Remote BT Device (Equalizer, Repeat, Shuffle, Scan, Status)*/
int
BtrCore_BTSetMediaProperty (
    void*       apBtConn,
    const char* apBtAdapterPath,
    char*       mediaProperty,
    char*       pValue
) {
    DBusMessage*    lpDBusMsg   = NULL;
    DBusMessage*    lpDBusReply = NULL;
    DBusError       lDBusErr;
    DBusMessageIter lDBusMsgIter;
    DBusMessageIter lDBusMsgIterValue;
    char*           mediaPlayerObjectPath = NULL;
    const char*     lDBusTypeAsString = DBUS_TYPE_STRING_AS_STRING;

    if (!gpDBusConn || (gpDBusConn != apBtConn) || !pValue)
        return -1;

    mediaPlayerObjectPath = BtrCore_BTGetMediaPlayerPath (gpDBusConn, apBtAdapterPath);

    if (mediaPlayerObjectPath == NULL) {
        return -1;
    }


    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             mediaPlayerObjectPath,
                                             "org.freedesktop.DBus.Properties",
                                             "Set");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_iter_init_append(lpDBusMsg, &lDBusMsgIter);
    dbus_message_iter_append_basic(&lDBusMsgIter, DBUS_TYPE_STRING, BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH);
    dbus_message_iter_append_basic(&lDBusMsgIter, DBUS_TYPE_STRING, &mediaProperty);
    dbus_message_iter_open_container(&lDBusMsgIter, DBUS_TYPE_VARIANT, lDBusTypeAsString, &lDBusMsgIterValue);
    dbus_message_iter_append_basic(&lDBusMsgIterValue, DBUS_TYPE_STRING, pValue);
    dbus_message_iter_close_container(&lDBusMsgIter, &lDBusMsgIterValue);

    dbus_error_init(&lDBusErr);
    lpDBusReply = dbus_connection_send_with_reply_and_block(gpDBusConn, lpDBusMsg, -1, &lDBusErr);
    dbus_message_unref(lpDBusMsg);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }

    dbus_message_unref(lpDBusReply);

    dbus_connection_flush(gpDBusConn);

    return 0;
}

/* Get Track information and place them in an array (Title, Artists, Album, number of tracks, tracknumber, duration, Genre)*/
// TODO : write a api that gets any properties irrespective of the objects' interfaces
int
BtrCore_BTGetTrackInformation (
    void*               apBtConn,
    const char*         apBtmediaPlayerObjectPath,
    stBTMediaTrackInfo* lpstBTMediaTrackInfo
) {
    unsigned int        ui32Value   = 0;
    char*               pcKey       = "\0";
    char*               pcValue     = "\0";
    char*               Track       = "Track";
    DBusMessage*        lpDBusMsg   = NULL;
    DBusMessage*        lpDBusReply = NULL;
    DBusPendingCall*    lpDBusPendC = NULL;
    DBusError           lDBusErr;
    DBusMessageIter     args;
    char*               mediaPlayerPath = BT_DBUS_BLUEZ_MEDIA_PLAYER_PATH;

    if (!gpDBusConn || (gpDBusConn != apBtConn)) {
        BTRCORELOG_ERROR ("DBus Connection Failure!!!"); 
        return -1;
    }

    if (NULL == apBtmediaPlayerObjectPath) {
        BTRCORELOG_ERROR ("Media Player Object is NULL!!!");
        return -1;
    }

    lpDBusMsg = dbus_message_new_method_call(BT_DBUS_BLUEZ_PATH,
                                             apBtmediaPlayerObjectPath,
                                             "org.freedesktop.DBus.Properties",
                                             "Get");

    if (!lpDBusMsg) {
        BTRCORELOG_ERROR ("Can't allocate new method call\n");
        return -1;
    }

    dbus_message_iter_init_append(lpDBusMsg, &args);
    dbus_message_append_args(lpDBusMsg, DBUS_TYPE_STRING, &mediaPlayerPath, DBUS_TYPE_STRING, &Track, DBUS_TYPE_INVALID);

    dbus_error_init(&lDBusErr);
    if (!dbus_connection_send_with_reply(gpDBusConn, lpDBusMsg, &lpDBusPendC, -1)) {
        BTRCORELOG_ERROR ("failed to send message");
        return -1;
    }

    dbus_connection_flush(gpDBusConn);
    dbus_message_unref(lpDBusMsg);

    dbus_pending_call_block(lpDBusPendC);
    lpDBusReply =  dbus_pending_call_steal_reply(lpDBusPendC);

    if (!lpDBusReply) {
        BTRCORELOG_ERROR ("lpDBusReply Null\n");
        btrCore_BTHandleDusError(&lDBusErr, __LINE__, __FUNCTION__);
        return -1;
    }
    dbus_pending_call_unref(lpDBusPendC);

    DBusMessageIter MsgIter, arrayMsgIter, dictMsgIter, element, elementBasic;
    int dbus_type = DBUS_TYPE_INVALID;

    dbus_message_iter_init(lpDBusReply, &MsgIter);                    // lpDBusMsg is pointer to dbus message received
    dbus_message_iter_recurse(&MsgIter, &arrayMsgIter);               // pointer to first element ARRAY of the dbus messge received
      dbus_message_iter_recurse(&arrayMsgIter, &dictMsgIter);         // pointer to first element DICTIONARY of the dbus messge received

    while (DBUS_TYPE_INVALID != (dbus_type = dbus_message_iter_get_arg_type(&dictMsgIter))) {
        if (DBUS_TYPE_DICT_ENTRY == dbus_type) {
            dbus_message_iter_recurse(&dictMsgIter,&element);         // pointer to element STRING of the dbus messge received

            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&element)) {
               dbus_message_iter_get_basic(&element, &pcKey);

               if (0==strcmp("Album", pcKey)) {        
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, &pcValue); 
                  strncpy(lpstBTMediaTrackInfo->pcAlbum, pcValue, BT_MAX_STR_LEN);
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->pcAlbum : %s\n", lpstBTMediaTrackInfo->pcAlbum);
               } else
               if (0==strcmp("Artist", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, &pcValue);
                  strncpy(lpstBTMediaTrackInfo->pcArtist, pcValue, BT_MAX_STR_LEN);
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->pcArtist : %s\n", lpstBTMediaTrackInfo->pcArtist);
               } else
               if (0==strcmp("Genre", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, &pcValue);
                  strncpy(lpstBTMediaTrackInfo->pcGenre, pcValue, BT_MAX_STR_LEN);
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->pcGenre : %s\n", lpstBTMediaTrackInfo->pcGenre);
               } else
               if (0==strcmp("Title", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, &pcValue);
                  strncpy(lpstBTMediaTrackInfo->pcTitle, pcValue, BT_MAX_STR_LEN);
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->pcTitle : %s\n", lpstBTMediaTrackInfo->pcTitle);
               } else
               if (0==strcmp("NumberOfTracks", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, (void*)&ui32Value);
                  lpstBTMediaTrackInfo->ui32NumberOfTracks = ui32Value;
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->ui32NumberOfTracks : %d\n", lpstBTMediaTrackInfo->ui32NumberOfTracks);
               } else
               if (0==strcmp("TrackNumber", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, (void*)&ui32Value);
                  lpstBTMediaTrackInfo->ui32TrackNumber = ui32Value;
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->ui32TrackNumber : %d\n", lpstBTMediaTrackInfo->ui32TrackNumber);
               } else
               if (0==strcmp("Duration", pcKey)) {
                  dbus_message_iter_next(&element);                   // next element is VARIANT of the dbus messge received
                  dbus_message_iter_recurse(&element, &elementBasic); // pointer to element STRING/UINT32 w.r.t dbus messge received here
                  dbus_message_iter_get_basic(&elementBasic, (void*)&ui32Value);
                  lpstBTMediaTrackInfo->ui32Duration = ui32Value;
                  BTRCORELOG_DEBUG ("lpstBTMediaTrackInfo->ui32Duration : %d\n", lpstBTMediaTrackInfo->ui32Duration);
               }
            }
         }
         if (!dbus_message_iter_has_next(&dictMsgIter)) {
            break;
         }
         else {
            dbus_message_iter_next(&dictMsgIter);
         }
      }

    return 0;
}

