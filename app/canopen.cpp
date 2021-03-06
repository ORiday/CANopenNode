/**
* @addtogroup io8000 template
* @{
* @addtogroup application
* @{
* @file canopen.cpp
* @copyright Neuberger Gebäudeautomation GmbH
* @author mwagner
* @brief CANopenNode
*
* @details \b Programm-Name template
* @details Diese Klasse stellt die Funktionen des CANopenNode Stack zur Verf"ugung
**/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "os/freertos/include/FreeRTOS.h"
#include "os/freertos/include/task.h"

#include "interface/log.h"
#include "interface/errors.h"

#include "interface/nbtyp.h"
#include "drivers/wdt.h"
#include "drivers/ad.h"
#include "drivers/led.h"
#include "drivers/daisychain.h"
#include "drivers/bootloader.h"
#include "os/terminal.h"
#include "drivers/system.h"

#include "CANopen.h"
#include "CO_freertos_threads.h"
#include "CO_OD.h"

#include "canopen.h"
#include "globdef.h"
#include "messages.h"
#include "os/main.h"

#ifndef UNIT_TEST

/*
 * Canopen Befehl zum Parametrieren des CAN Bus
 */
static BaseType_t canopen_terminal( char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString)
{
  /* Unsch"on: Instanz CO hartcodiert */
  return canopen.cmd_terminal(pcWriteBuffer, xWriteBufferLen, pcCommandString);
}
const CLI_Command_Definition_t terminal =
{
  .pcCommand = "canopen",
  .pcHelpString = "canopen -n x - address"  NEWLINE \
                  "  -b x baudrate"  NEWLINE,
  .pxCommandInterpreter = canopen_terminal,
  .cExpectedNumberOfParameters = 2
};

#endif

extern void housekeeping_main(void);

/* Klasse canopen */
class Canopen canopen;
/* Klassenvariablen */
QueueHandle_t Canopen::nmt_event_queue;

/** @defgroup Objektverzeichnishandler
 * Objektverzeichnishandler in der Reihenfolge, in der die Eintr"age
 * im OD abgelegt sind.
 *
 * F"ur jeden Eintrag muss es mind. einen Kommentar geben, warum keine
 * Funktion implementiert ist. Es kann ein Setter, ein Getter und eine
 * Callbackfunktion vorhanden sein, abh. von der Verwendung.
 *  @{
 */

/** @defgroup Communication Profile
 * Ab 0x1000 kommen die Eintr"age nach CiA 301
 *  @{
 */

/* 1000 - Device type
 * ro, predefined value
 */

/* 1001 - Error register
 * ro, wird durch Stack verwaltet, Zugriff per getter/setter
 */

/* 1003 - Pre-defined error field
 * ro, wird durch Stack verwaltet, Zugriff per getter/setter
 */

/* 1005 - COB-ID SYNC message
 * rw, wird durch Stack verwaltet
 */

/* 1006 - Communication cycle period
 * rw, wird durch Stack verwaltet
 */

/* 1007 - Synchronous window length
 * rw, wird durch Stack verwaltet
 */

/* 1008 - Manufacturer device name
 * const, predefined value
 */

/* 100a - Manufacturer software version
 * const, wird beim Startup gesetzt
 */

/** 1010 - Store parameters
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn Steuerung erfolgreich
 */
CO_SDO_abortCode_t Canopen::store_parameters_callback(CO_ODF_arg_t *p_odf_arg)
{
  CO_ReturnError_t result;
  Canopen_storage::storage_type_t type;
  u32 signature;
  u32 *p_data;

  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  p_data = reinterpret_cast<u32*>(p_odf_arg->data);

  signature = *p_data;
  /* Originalwert wieder herstellen */
  *p_data = *(reinterpret_cast<const u32*>(p_odf_arg->ODdataStorage));

  switch (p_odf_arg->subIndex) {
    case OD_1010_2_storeParameters_saveCommunicationRelatedParameters:
    case OD_1010_4_storeParameters_saveRuntime:
      /* Speichern nicht zul"assig */
      return CO_SDO_AB_DATA_TRANSF;
    default:
      break;
  }

  if (signature != 0x65766173) {
    /* keine Signatur "save" */
    return CO_SDO_AB_DATA_TRANSF;
  }

  switch (p_odf_arg->subIndex) {
    case OD_1010_1_storeParameters_saveAllParameters:
      result = storage.save(Canopen_storage::PARAMS);
      if (result != CO_ERROR_NO)  {
        return CO_SDO_AB_HW;
      }
      result = storage.save(Canopen_storage::TEST);
      if (result != CO_ERROR_NO)  {
        return CO_SDO_AB_HW;
      }
      result = storage.save(Canopen_storage::CALIB);
      if (result != CO_ERROR_NO)  {
        return CO_SDO_AB_HW;
      }
      return CO_SDO_AB_NONE;

    case OD_1010_3_storeParameters_saveApplicationRelatedParameters:
      type = Canopen_storage::PARAMS;
      break;
    case OD_1010_5_storeParameters_saveSerialNumber:
      if (OD_serialNumber.valid != false) {
        /* Seriennummer kann nur im ung"ultigen Zustand gesetzt werden */
        return CO_SDO_AB_DATA_TRANSF;
      }
      OD_serialNumber.valid = true;
      /* Seriennummer eintragen, Datum abschneiden */
      OD_identity.serialNumber =
          OD_serialNumber.serial % 100000000;
      type = Canopen_storage::SERIAL;
      break;
    case OD_1010_6_storeParameters_saveTestData:
      type = Canopen_storage::TEST;
      break;
    case OD_1010_7_storeParameters_saveCalibrationData:
      type = Canopen_storage::CALIB;
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  result = storage.save(type);
  if (result != CO_ERROR_NO)  {
    return CO_SDO_AB_HW;
  }

  return CO_SDO_AB_NONE;
}

/** 1011 - Restore default parameters
 *
 * Nach CiA301 wirkt sich der Restore auf die RAM Variablen erst nach einem
 * Neustart aus!
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn Steuerung erfolgreich
 */
CO_SDO_abortCode_t Canopen::restore_default_parameters_callback(CO_ODF_arg_t *p_odf_arg)
{
  u32 signature;
  u32 *p_data;

  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  p_data = reinterpret_cast<u32*>(p_odf_arg->data);

  signature = *p_data;
  /* Originalwert wieder herstellen */
  *p_data = *(reinterpret_cast<const u32*>(p_odf_arg->ODdataStorage));

  switch (p_odf_arg->subIndex) {
    case OD_1011_1_restoreDefaultParameters_restoreAllDefaultParameters:
    case OD_1011_3_restoreDefaultParameters_restoreApplicationRelatedParameters:
      /* Nur die App Parameter unterst"utzen den Load */
      if (signature != 0x64616F6C) {
        /* keine Signatur "load" */
        return CO_SDO_AB_DATA_TRANSF;
      }
      storage.restore(Canopen_storage::PARAMS);
      break;
    case OD_1011_2_restoreDefaultParameters_restoreCommunicationRelatedParameters:
    case OD_1011_4_restoreDefaultParameters_restoreRuntime:
    case OD_1011_5_restoreDefaultParameters_restoreSerialNumber:
    case OD_1011_6_restoreDefaultParameters_restoreTestData:
    case OD_1011_7_restoreDefaultParameters_restoreCalibrationData:
      /* Laden nicht zul"assig */
      return CO_SDO_AB_DATA_TRANSF;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  return CO_SDO_AB_NONE;
}

/** 1012 - COB-ID timestamp
 *
 * Keine Funktion. Minimalstimplementierung: Timestamp Producer wird abgelehnt
 *
 * @todo wird in einem Modul die Uhrzeit ben"otigt, so muss dieser Eintrag
 * implementiert, die Busfilter entsprechend parametriert und die Uhrzeit COB ID
 * empfangen werden.
 */
CO_SDO_abortCode_t Canopen::cob_id_timestamp_callback(CO_ODF_arg_t *p_odf_arg)
{
  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  if (*reinterpret_cast<u32*>(p_odf_arg->data) & 0x40000000U) {
    /* Timestamp Producer ablehnen */
    return CO_SDO_AB_DATA_TRANSF;
  }

  return CO_SDO_AB_NONE;
}

/* 1014 - COB-ID EMCY
 * const, predefined value
 */

/* 1015 - inhibit time EMCY
 * rw, wird durch Stack verwaltet
 */

/* 1016 - Consumer heartbeat time
 * rw, wird durch Stack verwaltet
 */

/* 1017 - Producer heartbeat time
 * rw, wird durch Stack verwaltet
 */

/* 1018-1 Vendor-ID
 * ro, predefined value
 */

/*
 * 1018-2 - Set Hardware Infos
 * ro, wird beim Startup gesetzt
 */

/*
 * 1018-3 - Set Firmwareversion
 * ro, wird beim Startup gesetzt
 */

/* 1018-4 Serial number
 * ro, wird beim Startup gesetzt
 */

/* 1019 - Synchronous counter overflow value
 * rw, wird durch Stack verwaltet
 */

/* 1020 - Verify configuration
 * rw, dient der Ablage eines Timestamp/Checksum durch den Master. Wird im Stack nicht
 * verwendet.
 */

/* 1026 - OS prompt
 * todo, nicht implementiert
 */

/* 1029 - Error behavior
 * rw, wird durch Stack verwaltet
 */

/* 1200 - SDO server parameter
 * rw, wird durch Stack verwaltet
 */

/* ab 1400 - RPDO communication parameter
 * rw, wird durch Stack verwaltet
 */

/* ab 1600 - RPDO mapping parameter
 * rw, wird durch Stack verwaltet
 */

/* ab 1800 - TPDO communication parameter
 * rw, wird durch Stack verwaltet
 */

/* ab 1A00 - TPDO mapping parameter
 * rw, wird durch Stack verwaltet
 */

/**
 * 1f51 - "Ubertragungssteuerung per Program Control
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn Steuerung erfolgreich
 */
CO_SDO_abortCode_t Canopen::program_control_callback(CO_ODF_arg_t *p_odf_arg)
{
  bootloader_program_control_t control;
  bootloader_state_t state;

  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  control = static_cast<bootloader_program_control_t>(*(p_odf_arg->data));

  state = bootloader_request(control, *p_active_nid);
  switch (state) {
    case BOOTLAODER_TIMEOUT:
      return CO_SDO_AB_TIMEOUT; //todo ist dieser Errorcode hier OK? Ist eigentlich SDO Timeout
    case BOOTLOADER_REBOOT:
      globals.request_reboot();
      return CO_SDO_AB_NONE;
    case BOOTLOADER_OK:
      return CO_SDO_AB_NONE;
    case BOOTLAODER_WRONG_STATE:
      return CO_SDO_AB_DATA_DEV_STATE;
    default:
      return CO_SDO_AB_INVALID_VALUE;
  }
}

/*
 * 1f56 - Set Program Software Identification
 * const, wird beim Startup gesetzt
 */

/** @}*/
/** @defgroup Manufacturer specific
 * Ab 0x2000 kommen neubergerspezifische Eintr"age
 *  @{
 */

/* 2000 - Template:
 * Platzhalter f"ur modulspezifische OD Eintr"age
 */

/* 2100 - Error status bits
 * ro, wird durch Stack verwaltet
 */

/* 2101 - CAN node ID
 * ro, wird beim Startup gesetzt
 */

/* 2102 - CAN bit rate
 * ro, wird beim Startup gesetzt
 */

/** 2108 - Temperature
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE
 */
CO_SDO_abortCode_t Canopen::temperature_callback(CO_ODF_arg_t *p_odf_arg)
{
  float temp;

  switch (p_odf_arg->subIndex) {
    case OD_2108_0_temperature_maxSubIndex:
      break;
    case OD_2108_1_temperature_coreTemperature:
      temp = globals.get_temp();
      *(reinterpret_cast<REAL32*>(p_odf_arg->data)) = temp;
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  return CO_SDO_AB_NONE;
}

/** 2109 - Voltage
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE
 */
CO_SDO_abortCode_t Canopen::voltage_callback(CO_ODF_arg_t *p_odf_arg)
{
  float vss;

  switch (p_odf_arg->subIndex) {
    case OD_2109_0_voltage_maxSubIndex:
      break;
    case OD_2109_1_voltage_supplyVoltage:
      vss = globals.get_vss();
      *(reinterpret_cast<REAL32*>(p_odf_arg->data)) = vss;
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  return CO_SDO_AB_NONE;
}

/** 2110 - Diagnose: Can Runtime Info
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn erfolgreich
 */
CO_SDO_abortCode_t Canopen::can_runtime_info_callback(CO_ODF_arg_t *p_odf_arg)
{
  can_info_t rti;
  can_state_t state;

  state = can_ioctl(CO->CANmodule[0]->driver, CAN_GET_INFO, &rti);
  if (state != CAN_OK) {
    return CO_SDO_AB_GENERAL;
  }

  switch (p_odf_arg->subIndex) {
    case OD_2110_0_canRuntimeInfo_maxSubIndex:
      break;
    case OD_2110_1_canRuntimeInfo_RXFrames:
      *(reinterpret_cast<UNSIGNED64*>(p_odf_arg->data)) =
          rti.rx.frames;
      break;
    case OD_2110_2_canRuntimeInfo_RXBytes:
      *(reinterpret_cast<UNSIGNED64*>(p_odf_arg->data)) =
          rti.rx.bytes;
      break;
    case OD_2110_3_canRuntimeInfo_RXDropped:
      *(reinterpret_cast<UNSIGNED32*>(p_odf_arg->data)) =
          rti.rx.dropped;
      break;
    case OD_2110_4_canRuntimeInfo_RXRec:
      *(reinterpret_cast<UNSIGNED16*>(p_odf_arg->data)) =
          rti.rx.rec;
      break;
    case OD_2110_5_canRuntimeInfo_RXQueueLength:
      *(reinterpret_cast<UNSIGNED16*>(p_odf_arg->data)) =
          rti.rx.queue_length;
      break;
    case OD_2110_6_canRuntimeInfo_TXFrames:
      *(reinterpret_cast<UNSIGNED64*>(p_odf_arg->data)) =
          rti.tx.frames;
      break;
    case OD_2110_7_canRuntimeInfo_TXBytes:
      *(reinterpret_cast<UNSIGNED64*>(p_odf_arg->data)) =
          rti.tx.bytes;
      break;
    case OD_2110_8_canRuntimeInfo_TXDropped:
      *(reinterpret_cast<UNSIGNED32*>(p_odf_arg->data)) =
          rti.tx.dropped;
      break;
    case OD_2110_9_canRuntimeInfo_TXTec:
      *(reinterpret_cast<UNSIGNED16*>(p_odf_arg->data)) =
          rti.tx.tec;
      break;
    case OD_2110_10_canRuntimeInfo_TXQueueLength:
      *(reinterpret_cast<UNSIGNED16*>(p_odf_arg->data)) =
          rti.tx.queue_length;
      break;
    case OD_2110_11_canRuntimeInfo_flags:
      *(reinterpret_cast<UNSIGNED32*>(p_odf_arg->data)) =
         (rti.busoff & 0x01) | (rti.passive & 0x01) << 1 | (rti.warning & 0x01) << 2;
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  return CO_SDO_AB_NONE;
}

/* 2111 - STM32 UID
 * const
 */

/** 2112 - Daisy Chain
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn erfolgreich
 */
CO_SDO_abortCode_t Canopen::daisychain_callback(CO_ODF_arg_t *p_odf_arg)
{
  daisy_next_t next;

  switch (p_odf_arg->subIndex) {
    case OD_2112_0_daisyChain_maxSubIndex:
      break;
    case OD_2112_1_daisyChain_shiftIn:
      if (p_odf_arg->reading == true) {
        return CO_SDO_AB_NONE;
      }
      if (*p_odf_arg->data != 0) {
        /* Schreibzugriff ung"ultig, vorherigen Wert wieder herstellen */
        *p_odf_arg->data = *(reinterpret_cast<const u8*>(p_odf_arg->ODdataStorage));
        return CO_SDO_AB_INVALID_VALUE;
      }
      break;
    case OD_2112_2_daisyChain_shiftOut:
      if ((static_cast<tBoolean>(*p_odf_arg->data)) == TRUE) {
        daisy_shift_so();
      }
      break;
    case OD_2112_3_daisyChain_next:
      daisy_read_so(&next);
      if (next == DAISY_NEXT_OCCUPIED) {
        *p_odf_arg->data = TRUE;
      } else {
        *p_odf_arg->data = FALSE;
      }
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }

  return CO_SDO_AB_NONE;
}

/* ab 2200 - Allgemein
 * Auf diese Eintr"age wird direkt aus den FBs zugegriffen
 */

/* 5000 - Test system:
 *
 */

/** 5000 - Serial number
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE wenn erfolgreich
 */
CO_SDO_abortCode_t Canopen::serial_number_callback(CO_ODF_arg_t *p_odf_arg)
{
  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  switch (p_odf_arg->subIndex) {
    case OD_5000_2_serialNumber_serial:
      if (OD_serialNumber.valid == true) {
        /* Schreibzugriff nicht mehr zulassen. Empfangenen Wert mit vorherigem ersetzen */
        (void)memcpy(p_odf_arg->data, p_odf_arg->ODdataStorage, p_odf_arg->dataLength);
        return CO_SDO_AB_READONLY;
      }
      break;
    default:
      return CO_SDO_AB_SUB_UNKNOWN;
  }
  return CO_SDO_AB_NONE;
}


/** @}*/
/** @defgroup Device Profile
 * Ab 0x6000 kommen Eingr"age abh. vom Ger"ateprofil
 *  @{
 */

/* ab 6000 - Profil
 * Auf diese Eintr"age wird direkt aus den FBs zugegriffen
 */

/** @}*/
/*
 * Ende Objektverzeichnis
 */
/** @}*/

/*
 * Private Methoden canopen
 */

/**
 * Schreibt bei NMT Zustands"anderung ein Event auf die per
 * <nmt_event()> vorgegebene Queue
 *
 * @param state neuer NMT Zustand
 */
void Canopen::nmt_state_callback(CO_NMT_internalState_t state)
{
  nmt_relay_event(static_cast<nmt_event_t>(state));
}

/**
 * Schreibt bei Schreibzugriff auf einen OD Eintrag ein Event auf die per
 * <od_event()> vorgegebene Queue
 *
 * @param p_odf_arg OD Eintrag
 * @return CO_SDO_AB_NONE
 */
CO_SDO_abortCode_t Canopen::generic_write_callback(CO_ODF_arg_t* p_odf_arg)
{
  od_event_t event;
  QueueHandle_t event_queue;

  if (p_odf_arg->reading == true) {
    return CO_SDO_AB_NONE;
  }

  /* Event ist Subset der Infos in CO_ODF_arg_t. Der Wert wird nicht "ubergeben
   * da die Struktur CO_ODF_arg_t nur innerhalb dieses Funktionsaufrufs g"ultig
   * ist -> Speicher f"ur gr"o"stm"ogliches Element in event_data_t notwendig um
   * Kopie abzulegen. */
  event.index = p_odf_arg->index;
  event.subindex = p_odf_arg->subIndex;

  event_queue = reinterpret_cast<QueueHandle_t>(p_odf_arg->object);

  (void)xQueueSend(event_queue, &event, 0);

  return CO_SDO_AB_NONE;
}

/**
 * Tr"agt Callback Funktion in Stack ein
 *
 * @param obj_dict_id Zugeh"origes Objekt im Objektverzeichnis
 * @param pODFunc Callback Funktion
 */
void Canopen::set_callback(u16 obj_dict_id,
                           CO_SDO_abortCode_t (*pODFunc)(CO_ODF_arg_t *ODF_arg))
{
  CO_OD_configure(CO->SDO[0], obj_dict_id, pODFunc, this, NULL, 0);
}

/**
 * Pointer auf OD Eintrag anhand Index/Subindex bestimmen
 *
 * @param index OD Index (z.B. aus CO_OD.h)
 * @param subindex OD Subindex (z.B. aus CO_OD.h)
 * @param size Größe des hinterlegten Eintrags in Bytes
 * @return Zeiger auf Eintrag oder NULL falls nicht existend
 */
void* Canopen::get_od_pointer(u16 index, u8 subindex, size_t size)
{
  u16 entry;
  u8 length;

  entry = CO_OD_find(CO->SDO[0], index);
  if (entry == 0xffff) {
    /* Existiert nicht */
    return NULL;
  }

  length = CO_OD_getLength(CO->SDO[0], entry, subindex);
  if (length != size) {
    return NULL;
  }

  return CO_OD_getDataPointer(CO->SDO[0], entry, subindex);
}

/**
 * Daisychain Shift In Eventhandler
 */
void Canopen::daisychain_event_callback(void)
{
  u8 nid;
  u8 shift_count;

  CO_LOCK_OD();
  OD_daisyChain.shiftIn ++;

  nid = OD_CANNodeID;
  shift_count = OD_daisyChain.shiftIn;
  CO_UNLOCK_OD();

  (void)CO_DaisyProducer_sendEvent(CO->DaisyProducer, shift_count, nid);
}

/**
 * LSS Node ID/Bitrate in NVM speichern
 *
 * @param nid Node ID
 * @param bit_rate Bitrate
 * @return true Erfolgreich
 */
bool Canopen::store_lss_config_callback(uint8_t nid, uint16_t bit_rate)
{
  u8 active_nid;
  CO_ReturnError_t result;

  /* F"urs Speichern der NID m"ussen wir einen Quertausch machen:
   * - aktive NID sichern
   * - zu speichernde NID eintragen, speichern
   * - aktive NID wieder eintragen
   */
  CO_LOCK_OD();
  active_nid = OD_CANNodeID;
  OD_CANNodeID = nid;
  CO_UNLOCK_OD();

  result = storage.save(Canopen_storage::COMMUNICATION);
  if (result == CO_ERROR_NO) {
    return true;
  }

  CO_LOCK_OD();
  OD_CANNodeID = active_nid;
  CO_UNLOCK_OD();

  return false;
}

#if defined(TPDO_MANUAL_CONTROL_EXTENSION) || defined(RPDO_MANUAL_CONTROL_EXTENSION)
/**
 * empfangenen RPDO an Anwendung weitergeben
 *
 * @param rpdo RPDO Objekt
 * @param message empfangener PDO
 */
void Canopen::rpdo_callback(const CO_RPDO_t *rpdo, const CO_CANrxMsg_t *message)
{
  if (p_rpdo != nullptr) {
    p_rpdo(p_rpdo_param, message->data, message->DLC);
  }
}
#else
void Canopen::rpdo_callback(const CO_RPDO_t *rpdo, const CO_CANrxMsg_t *message)
{
}
#endif

/**
 * Zeitkritische CANopen Abarbeitung
 */
void Canopen::timer_rx_thread(void)
{
  u8 wdt;

  wdt = wdt_register();

  while (TRUE) {
    wdt_trigger(wdt);
    CANrx_threadTmr_process();

    if ((timer_rx_suspend == true) || (globals.get_reboot() == true)) {
      timer_rx_suspend = false;
      wdt_pause(wdt);
      vTaskSuspend(NULL);
      wdt_resume(wdt);
    }
  }
}

/*
 * Public Methoden canopen
 *
 * Bechreibung innerhalb der Klassendeklaration
 */

/**
 * @defgroup Zugriffsfunktionen auf Netzwerkmanagement
 */

void Canopen::nmt_register(QueueHandle_t event_queue)
{
  Canopen::nmt_event_queue = event_queue;
  CO_NMT_initCallback(CO->NMT, &nmt_state_callback);
}

void Canopen::nmt_relay_event(nmt_event_t event)
{
  /* Mit dieser Implementierung ist nur ein Konsument der Events f"ur alle
   * Instanzen m"oglich. Falls mehr ben"otigt werden m"ussen die Queues in einer
   * Liste abgelegt werden */
  if (Canopen::nmt_event_queue != 0) {
    (void)xQueueSend(Canopen::nmt_event_queue, &event, 0);
  }
}

/** @}*/

/**
 * @defgroup Zugriffsfunktionen f"ur Objektverzeichnis
 * @todo Linux Version - CO_OD_find() kostet Zeit, Entries in Tabelle ablegen
 */

void Canopen::od_lock(void)
{
  CO_LOCK_OD();
}

void Canopen::od_unlock(void)
{
  CO_UNLOCK_OD();
}

void Canopen::od_get(u16 index, u8 subindex, bool* p_retval)
{
  u8 *p;

  p = (u8*)get_od_pointer(index, subindex, sizeof(*p));
  if (p == NULL) {
    *p_retval = false;
    return;
  }
  *p_retval = (*p == 0) ? false : true;
}

void Canopen::od_get(u16 index, u8 subindex, u8* p_retval)
{
  u8 *p;

  p = (u8*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, u16* p_retval)
{
  u16 *p;

  p = (u16*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, u32* p_retval)
{
  u32 *p;

  p = (u32*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, u64* p_retval)
{
  u64 *p;

  p = (u64*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, s8* p_retval)
{
  s8 *p;

  p = (s8*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, s16* p_retval)
{
  s16 *p;

  p = (s16*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, s32* p_retval)
{
  s32 *p;

  p = (s32*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, s64* p_retval)
{
  s64 *p;

  p = (s64*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, f32* p_retval)
{
  f32 *p;

  p = (f32*)get_od_pointer(index, subindex, sizeof(*p_retval));
  if (p == NULL) {
    *p_retval = 0;
    return;
  }
  *p_retval = *p;
}

void Canopen::od_get(u16 index, u8 subindex, const char** pp_visible_string)
{
  u16 entry;
  char *p;

  entry = CO_OD_find(CO->SDO[0], index);
  if (entry == 0xffff) {
    /* Existiert nicht */
    *pp_visible_string = NULL;
    return;
  }

  p = (char*)CO_OD_getDataPointer(CO->SDO[0], entry, subindex);
  if (p == NULL) {
    *pp_visible_string = NULL;
    return;
  }
  *pp_visible_string = p;
}

void Canopen::od_set(u16 index, u8 subindex, bool val)
{
  u8 *p;

  p = (u8*)get_od_pointer(index, subindex, sizeof(*p));
  if (p == NULL) {
    return;
  }
  *p = (val == true) ? 1 : 0;
}

void Canopen::od_set(u16 index, u8 subindex, u8 val)
{
  u8 *p;

  p = (u8*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, u16 val)
{
  u16 *p;

  p = (u16*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, u32 val)
{
  u32 *p;

  p = (u32*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, u64 val)
{
  u64 *p;

  p = (u64*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, s8 val)
{
  s8 *p;

  p = (s8*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, s16 val)
{
  s16 *p;

  p = (s16*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, s32 val)
{
  s32 *p;

  p = (s32*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, s64 val)
{
  s64 *p;

  p = (s64*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, f32 val)
{
  f32 *p;

  p = (f32*)get_od_pointer(index, subindex, sizeof(val));
  if (p == NULL) {
    return;
  }
  *p = val;
}

void Canopen::od_set(u16 index, u8 subindex, const char* p_visible_string)
{
  u16 entry;
  u16 length;
  char *p;

  entry = CO_OD_find(CO->SDO[0], index);
  if (entry == 0xffff) {
    /* Existiert nicht */
    return;
  }

  length = CO_OD_getLength(CO->SDO[0], entry, subindex);
  if (length == 0) {
    return;
  }

  p = (char*)CO_OD_getDataPointer(CO->SDO[0], entry, subindex);
  if (p == NULL) {
    return;
  }

  /* Der Quellstring muss entweder ein echter, nullterminierter String sein
   * oder die gleiche Länge haben wie der OD Eintrag. */
  (void)snprintf(p, length, p_visible_string);
}

void Canopen::od_event(u16 index, QueueHandle_t event_queue)
{
  CO_OD_configure(CO->SDO[0], index, generic_write_callback,
                  reinterpret_cast<void*>(event_queue), NULL, 0);
}

/** @}*/

/**
 * @defgroup Zugriffsfunktionen auf CANopen Emergency Funktionen
 */

bool Canopen::error_get(errorcode_t error)
{
  return CO_isError(CO->em, error);
}

void Canopen::error_set(errorcode_t error, u32 detail)
{
  u16 co_emergency;

  if (error < CO_EM_MANUFACTURER_START) {
    log_printf(LOG_ERR, ERR_CANOPEN_INVALID_ERROR, error);
    return;
  }

  switch (error) {
    case OUT_CUR_HIGH:
      co_emergency = CO_EMC401_OUT_CUR_HI;
      break;
    case OUT_SHORTED:
      co_emergency = CO_EMC401_OUT_SHORTED;
      break;
    case OUT_LOAD_DUMP:
      co_emergency = CO_EMC401_OUT_LOAD_DUMP;
      break;
    case IN_VOLT_HI:
      co_emergency = CO_EMC401_IN_VOLT_HI;
      break;
    case IN_VOLT_LOW:
      co_emergency = CO_EMC401_IN_VOLT_LOW;
      break;
    case INTERN_VOLT_HI:
      co_emergency = CO_EMC401_INTERN_VOLT_HI;
      break;
    case INTERN_VOLT_LO:
      co_emergency = CO_EMC401_INTERN_VOLT_LO;
      break;
    case OUT_VOLT_HIGH:
      co_emergency = CO_EMC401_OUT_VOLT_HIGH;
      break;
    case OUT_VOLT_LOW:
      co_emergency = CO_EMC401_OUT_VOLT_LOW;
      break;
    default:
      //todo k"onnen wir das irgendwie sinnvoll nutzen?  
      co_emergency = CO_EMC_DEVICE_SPECIFIC | error;
      break;
  }

  CO_errorReport(CO->em, error, co_emergency,
                 detail);
}

void Canopen::error_reset(errorcode_t error, u32 detail)
{
  if (error < CO_EM_MANUFACTURER_START) {
    log_printf(LOG_ERR, ERR_CANOPEN_INVALID_ERROR, error);
    return;
  }

  CO_errorReset(CO->em, error, detail);
}

/** @}*/

#if defined(TPDO_MANUAL_CONTROL_EXTENSION) || defined(RPDO_MANUAL_CONTROL_EXTENSION)
/**
 * @defgroup PDOs
 */

CO_ReturnError_t Canopen::tpdo_take_control(u16 tpdo_com_param_index)
{
  CO_ReturnError_t result;

  if (p_tpdo != nullptr) {
    return CO_ERROR_OUT_OF_MEMORY; //nur 1 Eintrag m"oglich
  }
  p_tpdo = CO_get_TPDO(CO, tpdo_com_param_index);
  if (p_tpdo == nullptr) {
    return CO_ERROR_PARAMETERS;
  }
  result = CO_TPDO_takeManualControl(reinterpret_cast<CO_TPDO_t*>(p_tpdo), true);
  return result;
}

void Canopen::tpdo_release_control(u16 id)
{
  (void)CO_TPDO_takeManualControl(reinterpret_cast<CO_TPDO_t*>(p_tpdo), false);
  p_tpdo = nullptr;
}

CO_ReturnError_t Canopen::tpdo_send(u16 id)
{
  TickType_t now;
  TickType_t difference_us;

  if (p_tpdo == nullptr) {
    return CO_ERROR_PARAMETERS;
  }
  now = xTaskGetTickCount();
  difference_us = (now - tpdo_called) * 1000;
  tpdo_called = now;

  reinterpret_cast<CO_TPDO_t*>(p_tpdo)->sendRequest = true;
  return CO_TPDO_process(reinterpret_cast<CO_TPDO_t*>(p_tpdo), nullptr, false, difference_us); //nicht zyklisch -> kein Heartbeat!!
}

CO_ReturnError_t Canopen::rpdo_take_control(u16 rpdo_com_param_index, void *param,
    void (*p)(void *param, const u8* p_data, u8 count))
{
  CO_RPDO_t *p_pdo;
  CO_ReturnError_t result;

  if (p == nullptr) {
    return CO_ERROR_PARAMETERS;
  }
  if (p_rpdo != nullptr) {
    return CO_ERROR_OUT_OF_MEMORY; //nur 1 Eintrag m"oglich
  }

  p_pdo = CO_get_RPDO(CO, rpdo_com_param_index);
  if (p_pdo == nullptr) {
    return CO_ERROR_PARAMETERS;
  }
  p_rpdo = p;
  p_rpdo_param = param;
  result = CO_RPDO_takeManualControl(p_pdo, true, this, rpdo_callback_wrapper);
  return result;
}

void Canopen::rpdo_release_control(u16 id)
{
  CO_RPDO_t *p_pdo;

  if (p_rpdo == nullptr) {
    return;
  }
  p_pdo = CO_get_RPDO(CO, id);
  if (p_pdo == nullptr) {
    return;
  }
  (void)CO_RPDO_takeManualControl(p_pdo, true, nullptr, nullptr);
  p_rpdo = nullptr;
}

/** @}*/
#else
CO_ReturnError_t Canopen::tpdo_take_control(u16 tpdo_com_param_index)
{
  return CO_ERROR_PARAMETERS;
}
void Canopen::tpdo_release_control(u16 id)
{
}
CO_ReturnError_t Canopen::tpdo_send(u16 id)
{
  return CO_ERROR_PARAMETERS;
}
CO_ReturnError_t Canopen::rpdo_take_control(u16 rpdo_com_param_index, void *param,
    void (*p)(void *param, const u8* p_data, u8 count))
{
  return CO_ERROR_PARAMETERS;
}
void Canopen::rpdo_release_control(u16 id)
{
}
#endif

/**
 * Einige Werte im OD werden zur Compile Time / Startup Time generiert. Diese
 * werden hier eingetragen.
 *
 * Diese Funktion darf nur vor dem Initialisieren des CO Stacks aufgerufen werden!
 */
void Canopen::od_set_defaults(void)
{
  const char *p_version;
  u32 id;
  u16 mod_type;
  u8 hw_rev;
  u8 main;
  u8 minor;
  u8 bugfix;
  u8 build;

  /* 100a - Manufacturer software version anhand dem in Git vorhandenen Versionsstring */
  p_version = globals.get_app_version_string();
  (void)snprintf(OD_manufacturerSoftwareVersion,
                 ODL_manufacturerSoftwareVersion_stringLength,
                 p_version);

  /* 1018-2 - Set Hardware Infos
   *
   * Diese Funktion "uberschreibt den Default aus dem OD Editor. Somit ist die
   * tats"achliche Hardwaretype lesbar.
   */
  mod_type = globals.get_type();
  hw_rev = globals.get_hw_rev();
  OD_identity.productCode = hw_rev << 16 | mod_type;

  /* 1018-3 - Set Firmwareversion anhand der in Git vorhandenen Versionsnummern */
  globals.get_app_version(&main, &minor, &bugfix, &build);
  OD_identity.revisionNumber = (u32)(main << 24 | minor << 16 | bugfix << 8 | build);

  /* 1018-4 Serial number
   *
   * Wir verwenden die Prozessor UID als Startwert
   */
  id = system_get_uid32();
  OD_identity.serialNumber = id;

  /* 1f56 - Set Program Software Identification */
  id = globals.get_app_checksum();
  OD_programSoftwareIdentification[0] = id;

  /* 2102 - CANopen Node ID */
  OD_CANNodeID = 0;

  /* 2102 - CAN bit rate */
  OD_CANBitRate = this->active_bit;

  /* 2111 - 96 Bit UID */
  system_get_uid96(&OD_UID[ODA_UID_word0], &OD_UID[ODA_UID_word1],
                   &OD_UID[ODA_UID_word2]);

  /* 2112 - Daisy Chain */
  OD_daisyChain.shiftIn = 0;

  /* 5000-2 - Serial number Default */
  OD_serialNumber.serial = OD_identity.serialNumber;
}

/**
 * CANopen Startwerte aus Festwertspeicher einladen.
 *
 * Ist ein Bereich nicht vorhanden oder fehlerhaft wird mit den im Firmwareimage
 * hinterlegten Daten gestartet.
 *
 * Diese Funktion darf nur vor dem Initialisieren des CO Stacks aufgerufen werden!
 */
void Canopen::od_load_start(void)
{
  CO_ReturnError_t co_result;

  co_result = storage.load(Canopen_storage::COMMUNICATION);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  }

  co_result = storage.load(Canopen_storage::PARAMS);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  }

  co_result = storage.load(Canopen_storage::RUNTIME);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  }

  co_result = storage.load(Canopen_storage::SERIAL);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  } else if (OD_serialNumber.valid == true) {
    /* Seriennummer eintragen, Datum abschneiden */
    OD_identity.serialNumber =
        OD_serialNumber.serial % 100000000;
  }

  co_result = storage.load(Canopen_storage::TEST);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  }

  co_result = storage.load(Canopen_storage::CALIB);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_NOTICE, NOTE_CANOPEN_NVMEM_LOAD, co_result);
  }
}

/**
 * Bestimmt anhand der aus der im OD stehenden (aus dem NVM geladenen)
 * NID und dem "Ubergabeparameter den LSS Startup
 *
 * @param p_pending_nid [in] vorgegebene NID [out] LSS Startup NID
 */
void Canopen::lss_check(u8* p_pending_nid)
{
  u8 persistent_nid;

  persistent_nid = 0;

  /* Abh. vom Aufrufparameter wird die Persistent NID verwendet */
  if (*p_pending_nid == 0) {
    persistent_nid = OD_CANNodeID; // Default 0 oder aus NVM geladen
    if ( ! CO_LSS_NODE_ID_VALID(persistent_nid)) {
      persistent_nid = CO_LSS_NODE_ID_ASSIGNMENT;
      *this->p_active_nid = CO_LSS_NODE_ID_ASSIGNMENT;
    }
    *p_pending_nid = persistent_nid;
  } else {
    /* "ubergebene NID beibehalten */
  }
}

/**
 * CANopenNode Initroutinen durchlaufen
 *
 * @return CO_ERROR_NO wenn erfolgreich
 */
CO_ReturnError_t Canopen::co_init(u8 pending_nid)
{
  CO_ReturnError_t co_result;

  /* CANopenNode, LSS initialisieren */
  co_result = CO_new();
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_ERR, ERR_CANOPEN_INIT_FAILED, co_result);
    return co_result;
  }
  co_result = CO_CANinit(CAN_MODULE_A, this->active_bit);
  if (co_result != CO_ERROR_NO) {
    CO_delete(CAN_MODULE_A);
    log_printf(LOG_ERR, ERR_CANOPEN_INIT_FAILED, co_result);
    return co_result;
  }
  co_result = CO_LSSinit(pending_nid, this->active_bit);
  if (co_result != CO_ERROR_NO) {
    CO_delete(CAN_MODULE_A);
    log_printf(LOG_ERR, ERR_CANOPEN_INIT_FAILED, co_result);
    return co_result;
  }
  CO_LSSslave_initCfgStoreCallback(CO->LSSslave, this, store_lss_config_callback_wrapper);

  /* start CAN */
  CO_CANsetNormalMode(CO->CANmodule[0]);

  return CO_ERROR_NO;
}

/**
 * LSS Node ID claiming wenn noch keine NID vergeben ist
 *
 * Blockiert und behandelt main() Housekeeping bis eine NID vergeben wurde!
 *
 * @param p_pending_nid [out] CANopen Startup NID
 */
void Canopen::lss_nid_assignment(u8* p_pending_nid)
{
#ifndef UNIT_TEST
  u16 dummy;

  /* Get Node ID */
  while (true) {
    CO_LSSslave_process(CO->LSSslave, this->active_bit, *this->p_active_nid,
                        &dummy, p_pending_nid);
    if ((*p_pending_nid != CO_LSS_NODE_ID_ASSIGNMENT) &&
        (CO_LSSslave_getState(CO->LSSslave) == CO_LSS_STATE_WAITING)) {
      log_printf(LOG_NOTICE, NOTE_LSS, *p_pending_nid);
      return;
    }

    housekeeping_main();
    (void)CO_CANrxWait(CO->CANmodule[0], this->main_interval);
  }

#else
  /* no LSS in unit testing */
  *p_pending_nid = 127;
#endif
}

CO_ReturnError_t Canopen::co_start(u8 pending_nid, u32 interval)
{
  CO_ReturnError_t co_result;
  BaseType_t os_result;

  this->worker_interval = interval;

  /* start CANopen */
  co_result = CO_CANopenInit(pending_nid);
  if (co_result != CO_ERROR_NO) {
    log_printf(LOG_ERR, ERR_CANOPEN_INIT_FAILED, co_result);
    return co_result;
  }
  *this->p_active_nid = pending_nid;

  /* Infos eintragen */
  threadMain_init(this->main_interval, xTaskGetCurrentTaskHandle()); /* ms Interval */

  /* OD Callbacks */
  set_callback(OD_1010_storeParameters, store_parameters_callback_wrapper);
  set_callback(OD_1011_restoreDefaultParameters, restore_default_parameters_callback_wrapper);
  set_callback(OD_1012_COB_IDTimestamp, cob_id_timestamp_callback_wrapper);
  set_callback(OD_1f51_programControl, program_control_callback_wrapper);
  set_callback(OD_2108_temperature, temperature_callback_wrapper);
  set_callback(OD_2109_voltage, voltage_callback_wrapper);
  set_callback(OD_2110_canRuntimeInfo, can_runtime_info_callback_wrapper);
  set_callback(OD_2112_daisyChain, daisychain_callback_wrapper);
  set_callback(OD_5000_serialNumber, serial_number_callback_wrapper);

  /* Durch Reset Communication werden alle Callbacks im Stack gel"oscht. Falls bereits
   * ein NMT Callback eingetragen war, wird dieser erneut eingetragen und
   * ein Event "Reset Communication" verteilt */
  if (Canopen::nmt_event_queue != 0) {
    nmt_register(Canopen::nmt_event_queue);
    nmt_relay_event(RESET_COMMUNICATION);
  }

  /* Configure Timer function for execution every <interval> millisecond */
  CANrx_threadTmr_init(this->worker_interval);
  if (timer_rx_handle != NULL) {
    /* Thread wurde bereits gestartet und ist laufbereit */
    vTaskResume(this->timer_rx_handle);
  } else {
    os_result = xTaskCreate(timer_rx_thread_wrapper, "CO",
                            THREAD_STACKSIZE_CANOPEN_TIMER, this,
                            THREAD_PRIORITY_CANOPEN_TIMER,
                            &this->timer_rx_handle);
    if (os_result != pdPASS) {
      log_printf(LOG_ERR, ERR_THREAD_CREATE_FAILED, "CO");
      return /* Let's assume */ CO_ERROR_OUT_OF_MEMORY;
    }
  }

  return CO_ERROR_NO;
}

CO_SDO_abortCode_t Canopen::serial_number_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->serial_number_callback(p_odf_arg);
}

CO_ReturnError_t Canopen::init(u8 nid, u32 interval)
{
  CO_ReturnError_t co_result;
  u8 pending_nid;

  od_set_defaults();
  od_load_start();

  pending_nid = nid;
  lss_check(&pending_nid);

  co_result = co_init(pending_nid);
  if (co_result != CO_ERROR_NO) {
    return co_result;
  }

#ifndef UNIT_TEST
  if (once != true) {
    (void)FreeRTOS_CLIRegisterCommand(&terminal);
    (void)daisy_init(MODTYPE_HW_TEMPLATE, daisychain_event_callback_wrapper, this);
  }
#endif

  lss_nid_assignment(&pending_nid);

  co_result = co_start(pending_nid, interval);
  if (co_result != CO_ERROR_NO) {
    return co_result;
  }

  if (once != true) {
    once = true;
    OD_powerOnCounter ++;
    (void)storage.save(Canopen_storage::RUNTIME);
  }

  return CO_ERROR_NO;
}

void Canopen::deinit(void)
{
  /* RX Handlerthread synchronisieren. Der Thread suspended sich dann
   * selbst. */
  timer_rx_suspend = true;
  while (timer_rx_suspend != false) {
    vTaskDelay(1);
  }

  /* NMT Subscribern den Zugriff auf CANopen Funktionen entziehen */
  nmt_relay_event(INITIALIZING);

  CO_delete(CAN_MODULE_A);
  reset = CO_RESET_NOT;
  *p_active_nid = 0;
  p_tpdo = nullptr;
  p_rpdo = nullptr;
}

void Canopen::process(void)
{
  u16 dummy;
  u8 pending_nid;
  CO_ReturnError_t result;
  CO_NMT_reset_cmd_t reset;

  threadMain_process(&reset);

  /* Reset auswerten. Der Reset kann von folgenden Stellen getriggert werden:
   * - Netzwerk
   * - Stack */
  if (reset != CO_RESET_NOT){
    log_printf(LOG_DEBUG, DEBUG_CANOPEN_RESET, reset);

    CO_LSSslave_process(CO->LSSslave, this->active_bit, *this->p_active_nid,
                        &dummy, &pending_nid);

    switch (reset) {
      case CO_RESET_COMM:
        deinit();
        result = init(pending_nid, this->worker_interval);
        if (result != CO_ERROR_NO) {
          globals.request_reboot();
        }
        break;
      case CO_RESET_APP:
        globals.request_reboot();
        break;
      case CO_RESET_QUIT:
        deinit();
        /* Keine CAN Kommunikation mehr m"oglich! Neuaufbau nur per Power Toggle */
        break;
      default:
        break;
    }
  }
}

#ifndef UNIT_TEST

/*
 * CANopen per CLI steuern
 */
BaseType_t Canopen::cmd_terminal( char *pcWriteBuffer,
                                  size_t xWriteBufferLen,
                                  const char *pcCommandString )
{
  int tmp;
  char opt;
  tResult result;
  can_state_t state;
  BaseType_t optarg_length;
  const char *p_opttmp;
  const char *p_optarg;

  /* Pr"ufung auf Parameteranzahl macht CLI da vorgegeben */
  p_opttmp = pcCommandString;
  result = terminal_get_opt(&p_opttmp, &opt);
  if (result != OK) {
    (void)snprintf(pcWriteBuffer, xWriteBufferLen, terminal_text_invalid_option,
                   reinterpret_cast<unsigned>(p_opttmp) -
                   reinterpret_cast<unsigned>(pcCommandString));
    return pdFALSE;
  }

  (void)terminal_get_opt_arg(&p_opttmp, &p_optarg, &optarg_length);
  tmp = strtoul(p_optarg, NULL, 0);

  switch (opt) {
    case 'n':
      /* nach Muster -n 22 */
      OD_CANNodeID = tmp;
      (void)storage.save(Canopen_storage::COMMUNICATION);
      globals.request_reboot(); //triggert Comm Params restore
      break;
    case 'b':
      /* nach Muster -b <can_baud_t>. 1 MBit = 0
       * Quick & dirty direkt in den Treiber, nicht speichernd */
      state = can_ioctl(CO->CANmodule[0]->driver, CAN_SET_BAUDRATE,
                        reinterpret_cast<void*>(&tmp));
      OD_CANBitRate = CO_LSS_bitTimingTableLookup[tmp];
      if (state != CAN_OK) {
        (void)snprintf(pcWriteBuffer, xWriteBufferLen, "Failed: %d" NEWLINE, state);
      }
      break;
    case 'r':
      /* nach Muster -r <type>. Wiederherstellung der Defaults erst nach Reset! */
      if (tmp >= Canopen_storage::TYPE_COUNT) {
        break;
      }
      (void)storage.restore(static_cast<Canopen_storage::storage_type_t>(tmp));
      break;
    default:
      (void)snprintf(pcWriteBuffer, xWriteBufferLen, terminal_text_unknown_option, opt);
      return pdFALSE;
  }
  return pdFALSE;
}

#endif

/*
 * Callback Wrapper
 */
void Canopen::timer_rx_thread_wrapper(void *p)
{
  reinterpret_cast<Canopen*>(p)->timer_rx_thread();
}

void Canopen::daisychain_event_callback_wrapper(void *p_object)
{
  return reinterpret_cast<Canopen*>(p_object)->daisychain_event_callback();
}

bool_t Canopen::store_lss_config_callback_wrapper(void *p_object, uint8_t nid, uint16_t bit_rate)
{
  return reinterpret_cast<Canopen*>(p_object)->store_lss_config_callback(nid, bit_rate);
}

void Canopen::rpdo_callback_wrapper(void *p_object, const CO_RPDO_t *rpdo, const CO_CANrxMsg_t *message)
{
  return reinterpret_cast<Canopen*>(p_object)->rpdo_callback(rpdo, message);
}

CO_SDO_abortCode_t Canopen::store_parameters_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->store_parameters_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::restore_default_parameters_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->restore_default_parameters_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::cob_id_timestamp_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->cob_id_timestamp_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::program_control_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->program_control_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::temperature_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->temperature_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::voltage_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->voltage_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::can_runtime_info_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->can_runtime_info_callback(p_odf_arg);
}

CO_SDO_abortCode_t Canopen::daisychain_callback_wrapper(CO_ODF_arg_t *p_odf_arg)
{
  return reinterpret_cast<Canopen*>(p_odf_arg->object)->daisychain_callback(p_odf_arg);
}

/**
* @} @}
**/
