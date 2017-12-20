/**
 * CAN module object for Linux socketCAN.
 *
 * This file is a template for other microcontrollers.
 *
 * @file        CO_driver.c
 * @ingroup     CO_driver
 * @author      Janez Paternoster, Martin Wagner
 * @copyright   2004 - 2015 Janez Paternoster, 2017 Neuberger Gebaeudeautomation GmbH
 *
 *
 * This file is part of CANopenNode, an opensource CANopen Stack.
 * Project home page is <https://github.com/CANopenNode/CANopenNode>.
 * For more information on CANopen see <http://www.can-cia.org/>.
 *
 * CANopenNode is free and open source software: you can redistribute
 * it and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Following clarification and special exception to the GNU General Public
 * License is included to the distribution terms of CANopenNode:
 *
 * Linking this library statically or dynamically with other modules is
 * making a combined work based on this library. Thus, the terms and
 * conditions of the GNU General Public License cover the whole combination.
 *
 * As a special exception, the copyright holders of this library give
 * you permission to link this library with independent modules to
 * produce an executable, regardless of the license terms of these
 * independent modules, and to copy and distribute the resulting
 * executable under terms of your choice, provided that you also meet,
 * for each linked independent module, the terms and conditions of the
 * license of that module. An independent module is a module which is
 * not derived from or based on this library. If you modify this
 * library, you may extend this exception to your version of the
 * library, but you are not obliged to do so. If you do not wish
 * to do so, delete this exception statement from your version.
 */


#ifndef CO_DRIVER_H
#define CO_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Include processor header file */
#include <stddef.h>         /* for 'NULL' */
#include <stdint.h>         /* for 'int8_t' to 'uint64_t' */
#include <stdbool.h>        /* for 'true', 'false' */

/**
 * @defgroup CO_driver Driver
 * @ingroup CO_CANopen
 * @{
 *
 * socketCAN specific code for CANopenNode.
 *
 * This file contains type definitions, functions and macros for:
 *  - Basic data types.
 *  - Receive and transmit buffers for CANopen messages.
 *  - Interaction with CAN module on the microcontroller.
 *  - CAN receive and transmit interrupts.
 *
 * This file is not only a CAN driver. There are no classic CAN queues for CAN
 * messages. This file provides direct connection with other CANopen
 * objects. It tries to provide fast responses and tries to avoid unnecessary
 * calculations and memory consumptions.
 *
 * CO_CANmodule_t contains an array of _Received message objects_ (of type
 * CO_CANrx_t) and an array of _Transmit message objects_ (of type CO_CANtx_t).
 * Each CANopen communication object owns one member in one of the arrays.
 * For example Heartbeat producer generates one CANopen transmitting object,
 * so it has reserved one member in CO_CANtx_t array.
 * SYNC module may produce sync or consume sync, so it has reserved one member
 * in CO_CANtx_t and one member in CO_CANrx_t array.
 *
 * ###Reception of CAN messages.
 * Before CAN messages can be received, each member in CO_CANrx_t must be
 * initialized. CO_CANrxBufferInit() is called by CANopen module, which
 * uses specific member. For example @ref CO_HBconsumer uses multiple members
 * in CO_CANrx_t array. (It monitors multiple heartbeat messages from remote
 * nodes.) It must call CO_CANrxBufferInit() multiple times.
 *
 * Main arguments to the CO_CANrxBufferInit() function are CAN identifier
 * and a pointer to callback function. Those two arguments (and some others)
 * are copied to the member of the CO_CANrx_t array.
 *
 * Callback function is a function, specified by specific CANopen module
 * (for example by @ref CO_HBconsumer). Each CANopen module defines own
 * callback function. Callback function will process the received CAN message.
 * It will copy the necessary data from CAN message to proper place. It may
 * also trigger additional task, which will further process the received message.
 * Callback function must be fast and must only make the necessary calculations
 * and copying.
 *
 * Received CAN messages are processed by CAN receive interrupt function.
 * After CAN message is received, function first tries to find matching CAN
 * identifier from CO_CANrx_t array. If found, then a corresponding callback
 * function is called.
 *
 * Callback function accepts two parameters:
 *  - object is pointer to object registered by CO_CANrxBufferInit().
 *  - msg  is pointer to CAN message of type CO_CANrxMsg_t.
 *
 * Callback function must return #CO_ReturnError_t: CO_ERROR_NO,
 * CO_ERROR_RX_OVERFLOW, CO_ERROR_RX_PDO_OVERFLOW, CO_ERROR_RX_MSG_LENGTH or
 * CO_ERROR_RX_PDO_LENGTH.
 *
 *
 * ###Transmission of CAN messages.
 * Before CAN messages can be transmitted, each member in CO_CANtx_t must be
 * initialized. CO_CANtxBufferInit() is called by CANopen module, which
 * uses specific member. For example Heartbeat producer must initialize it's
 * member in CO_CANtx_t array.
 *
 * CO_CANtxBufferInit() returns a pointer of type CO_CANtx_t, which contains buffer
 * where CAN message data can be written. CAN message is send with calling
 * CO_CANsend() function. If at that moment CAN transmit buffer inside
 * microcontroller's CAN module is free, message is copied directly to CAN module.
 * Otherwise CO_CANsend() function sets _bufferFull_ flag to true. Message will be
 * then sent by CAN TX interrupt as soon as CAN module is freed. Until message is
 * not copied to CAN module, its contents must not change. There may be multiple
 * _bufferFull_ flags in CO_CANtx_t array set to true. In that case messages with
 * lower index inside array will be sent first.
 */


/**
 * @name Critical sections
 * CANopenNode is designed to run in different threads, as described in README.
 * Threads are implemented differently in different systems. In microcontrollers
 * threads are interrupts with different priorities, for example.
 * It is necessary to protect sections, where different threads access to the
 * same resource. In simple systems interrupts or scheduler may be temporary
 * disabled between access to the shared resource. Otherwise mutexes or
 * semaphores can be used.
 *
 * ####Reentrant functions.
 * Functions CO_CANsend() from C_driver.h, CO_errorReport() from CO_Emergency.h
 * and CO_errorReset() from CO_Emergency.h may be called from different threads.
 * Critical sections must be protected. Eather by disabling scheduler or
 * interrupts or by mutexes or semaphores.
 *
 * ####Object Dictionary variables.
 * In general, there are two threads, which accesses OD variables: mainline and
 * timer. CANopenNode initialization and SDO server runs in mainline. PDOs runs
 * in faster timer thread. Processing of PDOs must not be interrupted by
 * mainline. Mainline thread must protect sections, which accesses the same OD
 * variables as timer thread. This care must also take the application. Note
 * that not all variables are allowed to be mapped to PDOs, so they may not need
 * to be protected. SDO server protects sections with access to OD variables.
 *
 * ####CAN receive thread.
 * It partially processes received CAN data and puts them into appropriate
 * objects. Objects are later processed. It does not need protection of
 * critical sections. There is one circumstance, where CANrx should be disabled:
 * After presence of SYNC message on CANopen bus, CANrx should be temporary
 * disabled until all receive PDOs are processed. See also CO_SYNC.h file and
 * CO_SYNC_initCallback() function.
 * @{
 */
    #define CO_LOCK_CAN_SEND()  /**< Lock critical section in CO_CANsend() */
    #define CO_UNLOCK_CAN_SEND()/**< Unlock critical section in CO_CANsend() */

    #define CO_LOCK_EMCY()      /**< Lock critical section in CO_errorReport() or CO_errorReset() */
    #define CO_UNLOCK_EMCY()    /**< Unlock critical section in CO_errorReport() or CO_errorReset() */

    #define CO_LOCK_OD()        /**< Lock critical section when accessing Object Dictionary */
    #define CO_UNLOCK_OD()      /**< Unock critical section when accessing Object Dictionary */
/** @} */

/**
 * @name Syncronisation functions
 * syncronisation for message buffer for communication between CAN receive and
 * message processing threads.
 *
 * If receive function runs inside IRQ, no further synchronsiation is needed.
 * Otherwise, some kind of synchronsiation has to be included. The following
 * example uses GCC builtin memory barrier __sync_synchronize(). A comprehensive
 * list can be found here: https://gist.github.com/leo-yuriev/ba186a6bf5cf3a27bae7
 * \code{.c}
    #define CANrxMemoryBarrier() {__sync_synchronize();}
 * \endcode
 * @{
 */
/** Memory barrier */
#define CANrxMemoryBarrier()
/** Check if new message has arrived */
#define IS_CANrxNew(rxNew) ((int)rxNew)
/** Set new message flag */
#define SET_CANrxNew(rxNew) {CANrxMemoryBarrier(); rxNew = (void*)1L;}
/** Clear new message flag */
#define CLEAR_CANrxNew(rxNew) {CANrxMemoryBarrier(); rxNew = (void*)0L;}
/** @} */

/**
 * @defgroup CO_dataTypes Data types
 * @{
 *
 * According to Misra C
 */
    /* int8_t to uint64_t are defined in stdint.h */
    typedef unsigned char           bool_t;     /**< bool_t */
    typedef float                   float32_t;  /**< float32_t */
    typedef long double             float64_t;  /**< float64_t */
    typedef char                    char_t;     /**< char_t */
    typedef unsigned char           oChar_t;    /**< oChar_t */
    typedef unsigned char           domain_t;   /**< domain_t */
/** @} */


/**
 * Return values of some CANopen functions. If function was executed
 * successfully it returns 0 otherwise it returns <0.
 */
typedef enum{
    CO_ERROR_NO                 = 0,    /**< Operation completed successfully */
    CO_ERROR_ILLEGAL_ARGUMENT   = -1,   /**< Error in function arguments */
    CO_ERROR_OUT_OF_MEMORY      = -2,   /**< Memory allocation failed */
    CO_ERROR_TIMEOUT            = -3,   /**< Function timeout */
    CO_ERROR_ILLEGAL_BAUDRATE   = -4,   /**< Illegal baudrate passed to function CO_CANmodule_init() */
    CO_ERROR_RX_OVERFLOW        = -5,   /**< Previous message was not processed yet */
    CO_ERROR_RX_PDO_OVERFLOW    = -6,   /**< previous PDO was not processed yet */
    CO_ERROR_RX_MSG_LENGTH      = -7,   /**< Wrong receive message length */
    CO_ERROR_RX_PDO_LENGTH      = -8,   /**< Wrong receive PDO length */
    CO_ERROR_TX_OVERFLOW        = -9,   /**< Previous message is still waiting, buffer full */
    CO_ERROR_TX_BUSY            = -10,  /**< Sending rejected because driver is busy. Try again */
    CO_ERROR_TX_PDO_WINDOW      = -11,  /**< Synchronous TPDO is outside window */
    CO_ERROR_TX_UNCONFIGURED    = -12,  /**< Transmit buffer was not confugured properly */
    CO_ERROR_PARAMETERS         = -13,  /**< Error in function function parameters */
    CO_ERROR_DATA_CORRUPT       = -14,  /**< Stored data are corrupt */
    CO_ERROR_CRC                = -15,  /**< CRC does not match */
    CO_ERROR_WRONG_NMT_STATE    = -16,  /**< Command can't be processed in current state */
    CO_ERROR_SYSCALL            = -17   /**< Syscall failed */
}CO_ReturnError_t;


/**
 * CAN receive message structure as aligned in socketCAN.
 */
typedef struct{
    /** CAN identifier. It must be read through CO_CANrxMsg_readIdent() function. */
    uint32_t            ident;
    uint8_t             DLC ;           /**< Length of CAN message */
    uint8_t             padding[3];     /**< ensure alignment */
    uint8_t             data[8];        /**< 8 data bytes */
}CO_CANrxMsg_t;


/**
 * Received message object
 */
typedef struct{
    uint32_t            ident;          /**< Standard CAN Identifier (bits 0..10) + RTR (bit 11) */
    uint32_t            mask;           /**< Standard Identifier mask with same alignment as ident */
    void               *object;         /**< From CO_CANrxBufferInit() */
    void              (*pFunct)(void *object, const CO_CANrxMsg_t *message);  /**< From CO_CANrxBufferInit() */
}CO_CANrx_t;


/**
 * Transmit message object. No difference to rxMsg in this driver.
 */
typedef CO_CANrxMsg_t CO_CANtx_t;


/**
 * CAN module object. It may be different in different microcontrollers.
 */
typedef struct{
    int32_t             CANbaseAddress; /**< From CO_CANmodule_init() */
    CO_CANrx_t         *rxArray;        /**< From CO_CANmodule_init() */
    uint16_t            rxSize;         /**< From CO_CANmodule_init() */
    struct can_filter  *rxFilter;       /**< socketCAN filter list, one per rx buffer */
    uint32_t            rxDropCount;    /**< messages dropped on rx socket queue */
    CO_CANtx_t         *txArray;        /**< From CO_CANmodule_init() */
    uint16_t            txSize;         /**< From CO_CANmodule_init() */
    volatile bool_t     CANnormal;      /**< CAN module is in normal mode */
    void               *em;             /**< Emergency object */
    int fd;
}CO_CANmodule_t;


/**
 * Endianes.
 *
 * Depending on processor or compiler architecture, one of the two macros must
 * be defined: CO_LITTLE_ENDIAN or CO_BIG_ENDIAN. CANopen itself is little endian.
 */
#ifdef __BYTE_ORDER
#if __BYTE_ORDER == __LITTLE_ENDIAN
    #define CO_LITTLE_ENDIAN
#else
    #define CO_BIG_ENDIAN
#endif
#endif

/**
 * Request CAN configuration (stopped) mode and *wait* untill it is set.
 *
 * @param CANbaseAddress CAN module base address.
 */
void CO_CANsetConfigurationMode(int32_t CANbaseAddress);


/**
 * Request CAN normal (opearational) mode and *wait* untill it is set.
 *
 * @param CANmodule This object.
 */
void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule);


/**
 * Initialize CAN module object and open socketCAN connection.
 *
 * Function must be called in the communication reset section. CAN module must
 * be in Configuration Mode before.
 *
 * @param CANmodule This object will be initialized.
 * @param CANbaseAddress CAN module base address.
 * @param rxArray Array for handling received CAN messages
 * @param rxSize Size of the above array. Must be equal to number of receiving CAN objects.
 * @param txArray Array for handling transmitting CAN messages
 * @param txSize Size of the above array. Must be equal to number of transmitting CAN objects.
 * @param CANbitRate not supported in this driver. Needs to be set by OS
 *
 * Return #CO_ReturnError_t: CO_ERROR_NO, CO_ERROR_ILLEGAL_ARGUMENT or
 * CO_ERROR_SYSCALL.
 */
CO_ReturnError_t CO_CANmodule_init(
        CO_CANmodule_t         *CANmodule,
        int32_t                 CANbaseAddress,
        CO_CANrx_t              rxArray[],
        uint16_t                rxSize,
        CO_CANtx_t              txArray[],
        uint16_t                txSize,
        uint16_t                CANbitRate);


/**
 * Close socketCAN connection. Call at program exit.
 *
 * @param CANmodule CAN module object.
 */
void CO_CANmodule_disable(CO_CANmodule_t *CANmodule);


/**
 * Read CAN identifier from received message
 *
 * @param rxMsg Pointer to received message
 * @return 11-bit CAN standard identifier.
 */
uint16_t CO_CANrxMsg_readIdent(const CO_CANrxMsg_t *rxMsg);


/**
 * Configure CAN message receive buffer.
 *
 * Function configures specific CAN receive buffer. It sets CAN identifier
 * and connects buffer with specific object. Function must be called for each
 * member in _rxArray_ from CO_CANmodule_t.
 *
 * @param CANmodule This object.
 * @param index Index of the specific buffer in _rxArray_.
 * @param ident 11-bit standard CAN Identifier.
 * @param mask 11-bit mask for identifier. Most usually set to 0x7FF.
 * Received message (rcvMsg) will be accepted if the following
 * condition is true: (((rcvMsgId ^ ident) & mask) == 0).
 * @param rtr If true, 'Remote Transmit Request' messages will be accepted.
 * @param object CANopen object, to which buffer is connected. It will be used as
 * an argument to pFunct. Its type is (void), pFunct will change its
 * type back to the correct object type.
 * @param pFunct Pointer to function, which will be called, if received CAN
 * message matches the identifier. It must be fast function.
 *
 * Return #CO_ReturnError_t: CO_ERROR_NO CO_ERROR_ILLEGAL_ARGUMENT or
 * CO_ERROR_OUT_OF_MEMORY (not enough masks for configuration).
 */
CO_ReturnError_t CO_CANrxBufferInit(
        CO_CANmodule_t         *CANmodule,
        uint32_t                index,
        uint32_t                ident,
        uint32_t                mask,
        bool_t                  rtr,
        void                   *object,
        void                  (*pFunct)(void *object, const CO_CANrxMsg_t *message));


/**
 * Configure CAN message transmit buffer.
 *
 * Function configures specific CAN transmit buffer. Function must be called for
 * each member in _txArray_ from CO_CANmodule_t.
 *
 * @param CANmodule This object.
 * @param index Index of the specific buffer in _txArray_.
 * @param ident 11-bit standard CAN Identifier.
 * @param rtr If true, 'Remote Transmit Request' messages will be transmitted.
 * @param noOfBytes Length of CAN message in bytes (0 to 8 bytes).
 * @param syncFlag not supported
 *
 * @return Pointer to CAN transmit message buffer. 8 bytes data array inside
 * buffer should be written, before CO_CANsend() function is called.
 * Zero is returned in case of wrong arguments.
 */
CO_CANtx_t *CO_CANtxBufferInit(
        CO_CANmodule_t         *CANmodule,
        uint32_t                index,
        uint32_t                ident,
        bool_t                  rtr,
        uint8_t                 noOfBytes,
        bool_t                  syncFlag);


/**
 * Send CAN message.
 *
 * @param CANmodule This object.
 * @param buffer Pointer to transmit buffer, returned by CO_CANtxBufferInit().
 * Data bytes must be written in buffer before function call.
 *
 * @return #CO_ReturnError_t: CO_ERROR_NO, CO_ERROR_TX_OVERFLOW or
 * CO_ERROR_TX_PDO_WINDOW (Synchronous TPDO is outside window).
 */
CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer);

/**
 * The same as #CO_CANsend(), but ensures that there is enough space remaining
 * in the driver for more important messages.
 *
 * The default threshold is 50%, or at least 1 message buffer. If sending
 * would violate those limits, #CO_ERROR_TX_OVERFLOW is returned and the
 * message will not be sent.
 *
 * @param CANmodule This object.
 * @param buffer Pointer to transmit buffer, returned by CO_CANtxBufferInit().
 * Data bytes must be written in buffer before function call.
 *
 * @return #CO_ReturnError_t: CO_ERROR_NO, CO_ERROR_TX_OVERFLOW or
 * CO_ERROR_TX_PDO_WINDOW (Synchronous TPDO is outside window).
 */
CO_ReturnError_t CO_CANCheckSend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer);

/**
 * Clear all synchronous TPDOs from CAN module transmit buffers.
 * This function is not supported in this driver.
 */
void CO_CANclearPendingSyncPDOs(CO_CANmodule_t *CANmodule);


/**
 * Verify all errors of CAN module.
 * This function is not supported in this driver. Error checking is done
 * inside <CO_CANrxWait()>.
 */
void CO_CANverifyErrors(CO_CANmodule_t *CANmodule);


/**
 * Functions receives CAN messages. It is blocking.
 *
 * This function can be used in two ways
 * - automatic mode (call callback that is set by #CO_CANrxBufferInit() function)
 * - manual mode (evaluate message filters, return received message)
 *
 * Both modes can be combined.
 *
 * @param CANmodule This object.
 * @param buffer [out] storage for received message or _NULL_
 * @retval >= 0 index of received message in array set by #CO_CANmodule_init()
 *         _rxArray_, copy available in _buffer_
 * @retval -1 no message received
 */
int32_t CO_CANrxWait(CO_CANmodule_t *CANmodule, CO_CANrxMsg_t *buffer);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

/** @} */
#endif
