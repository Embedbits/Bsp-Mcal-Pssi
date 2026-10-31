/**
 * \author Mr.Nobody
 * \defgroup Pssi Pssi
 * \brief Pssi module
 */

/**
 * \author Mr.Nobody
 * \file Pssi_Types.h
 * \ingroup Pssi
 * \brief Pssi module global types definition
 *
 * This file contains the types definitions used across the module and are
 * available for other modules through the Port file. STM32H5 has one PSSI
 * instance on the lines H523/H533/H562/H563/H573/H5E4/H5E5/H5F4/H5F5 and no
 * PSSI at all on H503/H543/H553 (see the module README availability table;
 * every public function of the module then only returns PSSI_REQUEST_ERROR).
 * This first implementation supports the 8-bit data
 * bus only (PSSI_D0..PSSI_D7, EDM=00): see pssi_DataWidth_t.
 *
 */

#ifndef PSSI_PSSI_TYPES_H
#define PSSI_PSSI_TYPES_H
/* ============================== INCLUDES ================================== */
#include "stdint.h"                         /* Module types definition        */
#include "Gpio_Port.h"                      /* GPIO Port module definitions   */
#include "Nvic_Port.h"                      /* NVIC Port module definitions   */
#include "Gpdma_Port.h"                     /* GPDMA Port module definitions  */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Null pointer definition */
#define PSSI_NULL_PTR                           ( ( void* ) 0u )

/** Count of data pins of the 8-bit bus implemented by this first version */
#define PSSI_DATA_PIN_CNT                        ( 8u )

/** Port identification bit offset in an encoded pin value */
#define PSSI_BIT_MASK_PORT_BIT_OFFSET            ( 10u )

/** Pin identification bit offset in an encoded pin value */
#define PSSI_BIT_MASK_PIN_BIT_OFFSET             ( 5u )

/** Alternate function identification bit offset in an encoded pin value */
#define PSSI_BIT_MASK_AF_BIT_OFFSET              ( 0u )

/** Data line number identification bit offset in an encoded data pin value */
#define PSSI_BIT_MASK_LINE_BIT_OFFSET            ( 15u )

/* ========================== EXPORTED MACROS ================================ */

/** Encode a single-purpose (PDCK / DE / RDY) pin configuration into a single bit-mask */
#define PSSI_PIN_BIT_MASK_ENCODE( PORT_ID, PIN_ID, AF_ID ) \
            ( ( (uint32_t)(PORT_ID) << PSSI_BIT_MASK_PORT_BIT_OFFSET ) | \
              ( (uint32_t)(PIN_ID)  << PSSI_BIT_MASK_PIN_BIT_OFFSET  ) | \
              ( (uint32_t)(AF_ID)   << PSSI_BIT_MASK_AF_BIT_OFFSET   )   )

/** Encode a data line pin configuration (line number D0..D7) into a single bit-mask */
#define PSSI_DATA_PIN_BIT_MASK_ENCODE( LINE_NO, PORT_ID, PIN_ID, AF_ID ) \
            ( ( (uint32_t)(LINE_NO)  << PSSI_BIT_MASK_LINE_BIT_OFFSET ) | \
              ( (uint32_t)(PORT_ID) << PSSI_BIT_MASK_PORT_BIT_OFFSET  ) | \
              ( (uint32_t)(PIN_ID)  << PSSI_BIT_MASK_PIN_BIT_OFFSET   ) | \
              ( (uint32_t)(AF_ID)   << PSSI_BIT_MASK_AF_BIT_OFFSET    )   )

/** Extract the port identification from an encoded pin value */
#define PSSI_BIT_MASK_DECODE_PORT( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> PSSI_BIT_MASK_PORT_BIT_OFFSET ) & 0x1Fu )

/** Extract the pin identification from an encoded pin value */
#define PSSI_BIT_MASK_DECODE_PIN( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> PSSI_BIT_MASK_PIN_BIT_OFFSET ) & 0x1Fu )

/** Extract the alternate function identification from an encoded pin value */
#define PSSI_BIT_MASK_DECODE_AF( CODED_VAL ) \
            ( ( (uint32_t)(CODED_VAL) >> PSSI_BIT_MASK_AF_BIT_OFFSET ) & 0x1Fu )

/* ============================== TYPEDEFS ================================== */

/** Type signaling major version of SW module */
typedef uint8_t pssi_MajorVersion_t;

/** Type signaling minor version of SW module */
typedef uint8_t pssi_MinorVersion_t;

/** Type signaling patch version of SW module */
typedef uint8_t pssi_PatchVersion_t;

/** Module version */
typedef struct
{
    pssi_MajorVersion_t Major; /**< Major version number */
    pssi_MinorVersion_t Minor; /**< Minor version number */
    pssi_PatchVersion_t Patch; /**< Patch version number */
}   pssi_ModuleVersion_t;

/** Result of a module request */
typedef enum
{
    PSSI_REQUEST_OK    = 0u,                /**< The request succeeded */
    PSSI_REQUEST_ERROR                      /**< The request failed (bad parameter, bad state, hardware timeout) */
}   pssi_RequestState_t;

/** Generic active / inactive function state of the module */
typedef enum
{
    PSSI_FUNCTION_INACTIVE = 0u,            /**< The function is inactive */
    PSSI_FUNCTION_ACTIVE                    /**< The function is active */
}   pssi_FunctionState_t;

/** Generic active / inactive flag state of the module */
typedef enum
{
    PSSI_FLAG_INACTIVE = 0u,                /**< The flag is not set */
    PSSI_FLAG_ACTIVE                        /**< The flag is set */
}   pssi_FlagState_t;

/* ========================= PIN ENUMERATIONS, EARLY ========================= */
/* Pssi_Pins.h uses the bit-mask encode macros defined just above, and its items
 * (pssi_ClockPin_t, pssi_DePin_t, pssi_RdyPin_t, pssi_DataPin_t) are used by
 * pssi_Config_t below, so it is included here rather than at the end of this file. */
#include "Pssi_Pins.h"                       /* Generated pin enumerations     */

/** Parallel data bus width. Only the 8-bit bus (PSSI_D0..PSSI_D7, EDM=00) is implemented in this
 *  first STM32H5 version (see the module card); the 16-bit bus is not offered until the D8..D15
 *  pin tables are generated and tested. */
typedef enum
{
    PSSI_DATA_WIDTH_8 = 0u, /**< 8-bit bus (PSSI_D0..D7), the only width implemented */
    PSSI_DATA_WIDTH_CNT     /**< Number of data width items                          */
}   pssi_DataWidth_t;

/** Direction of a transfer (PSSI_CR.OUTEN) */
typedef enum
{
    PSSI_DIRECTION_RECEIVE = 0u,             /**< Data is input, synchronous with PSSI_PDCK */
    PSSI_DIRECTION_TRANSMIT,                 /**< Data is output, synchronous with PSSI_PDCK */
    PSSI_DIRECTION_CNT                       /**< Number of direction items */
}   pssi_Direction_t;

/** Active edge of the parallel data clock (PSSI_CR.CKPOL) */
typedef enum
{
    PSSI_CLOCK_EDGE_FALLING = 0u,            /**< Falling edge active for inputs, rising for outputs */
    PSSI_CLOCK_EDGE_RISING,                  /**< Rising edge active for inputs, falling for outputs */
    PSSI_CLOCK_EDGE_CNT                      /**< Number of clock edge items */
}   pssi_ClockEdge_t;

/** Use of the data enable (PSSI_DE) and ready (PSSI_RDY) handshake signals (PSSI_CR.DERDYCFG).
 *  Only the four non-bidirectional configurations of the register are offered in this first
 *  version (see the module card: DE/RDY handshake, not the single-pin bidirectional mode). */
typedef enum
{
    PSSI_HANDSHAKE_NONE = 0u,                /**< PSSI_DE and PSSI_RDY both disabled (DERDYCFG=000) */
    PSSI_HANDSHAKE_RDY,                      /**< Only PSSI_RDY enabled (DERDYCFG=001) */
    PSSI_HANDSHAKE_DE,                       /**< Only PSSI_DE enabled (DERDYCFG=010) */
    PSSI_HANDSHAKE_DE_RDY,                   /**< Both PSSI_DE and PSSI_RDY enabled (DERDYCFG=011) */
    PSSI_HANDSHAKE_CNT                       /**< Number of handshake items */
}   pssi_Handshake_t;

/** Active level of a handshake signal (PSSI_CR.DEPOL / PSSI_CR.RDYPOL) */
typedef enum
{
    PSSI_POLARITY_ACTIVE_LOW = 0u, /**< The signal is active low (reset value) */
    PSSI_POLARITY_ACTIVE_HIGH,     /**< The signal is active high              */
    PSSI_POLARITY_CNT              /**< Number of polarity items               */
}   pssi_Polarity_t;

/** Transfer state machine, see Pssi_Get_XferState */
typedef enum
{
    PSSI_XFER_IDLE = 0u,                      /**< No transfer requested */
    PSSI_XFER_RECEIVING,                      /**< A receive transfer is running */
    PSSI_XFER_TRANSMITTING,                   /**< A transmit transfer is running */
    PSSI_XFER_ERROR,                          /**< The last transfer ended with an error */
    PSSI_XFER_STATE_CNT                       /**< Number of transfer state items */
}   pssi_XferState_t;

/** Error bit mask type reported to \ref pssi_ErrorCallback_t */
typedef uint32_t pssi_XferError_t;

/** Overrun error (receive mode): the FIFO was full when a clock edge arrived, data was lost */
#define PSSI_ERROR_OVERRUN                       ( 0x00000001u )

/** Underrun error (transmit mode): the FIFO was empty when a clock edge arrived */
#define PSSI_ERROR_UNDERRUN                      ( 0x00000002u )

/** DMA transfer error reported by Gpdma (see Gpdma_Port.h GPDMA_ERROR_*) */
#define PSSI_ERROR_DMA                            ( 0x00000004u )

/** Transfer byte count type */
typedef uint32_t pssi_DataCnt_t;

/** Transferred data storage element type (one byte per data clock on the 8-bit bus) */
typedef uint8_t pssi_Data_t;

/** Interrupt priority type of the instance (forwarded to Nvic) */
typedef nvic_IrqPrio_t pssi_IrqPrio_t;

/** DMA peripheral identification type (forwarded to Gpdma) */
typedef gpdma_PeriphId_t pssi_DmaPeriphId_t;

/** DMA channel identification type (forwarded to Gpdma) */
typedef gpdma_ChannelId_t pssi_DmaChannelId_t;

/** DMA channel priority type (forwarded to Gpdma) */
typedef gpdma_Priority_t pssi_DmaPriority_t;

/** Callback reporting the end of a transfer without error, byteCnt is the count moved */
typedef void ( *pssi_XferCompleteCallback_t )( pssi_DataCnt_t byteCnt );

/** Callback reporting a transfer error, see PSSI_ERROR_* for errorMask */
typedef void ( *pssi_ErrorCallback_t )( pssi_XferError_t errorMask );

/** Configuration structure of the PSSI instance, see Pssi_Init / Pssi_Get_DefaultConfig */
typedef struct
{
    pssi_DataWidth_t            DataWidth;                    /**< Parallel bus width (8-bit only)          */
    pssi_ClockEdge_t            ClockEdge;                    /**< Parallel data clock active edge          */
    pssi_Handshake_t            Handshake;                    /**< Use of PSSI_DE / PSSI_RDY                */
    pssi_Polarity_t             DePolarity;                   /**< PSSI_DE active level                     */
    pssi_Polarity_t             RdyPolarity;                  /**< PSSI_RDY active level                    */
    pssi_IrqPrio_t              IrqPriority;                  /**< NVIC priority of the shared IRQ          */
    pssi_DmaPeriphId_t          DmaPeriphId;                  /**< Gpdma peripheral of the transfer         */
    pssi_DmaChannelId_t         DmaChannelId;                 /**< Gpdma channel of the transfer            */
    pssi_DmaPriority_t          DmaPriority;                  /**< Gpdma channel priority                   */
    pssi_ClockPin_t             ClockPin;                     /**< PSSI_PDCK pin                            */
    pssi_DePin_t                DePin;                        /**< PSSI_DE pin (handshake DE/DE_RDY only)   */
    pssi_RdyPin_t               RdyPin;                       /**< PSSI_RDY pin (handshake RDY/DE_RDY only) */
    pssi_DataPin_t              DataPin[ PSSI_DATA_PIN_CNT ]; /**< D0..D7 pins                              */
    pssi_XferCompleteCallback_t XferCompleteCallback;         /**< May be PSSI_NULL_PTR                     */
    pssi_ErrorCallback_t        ErrorCallback;                /**< May be PSSI_NULL_PTR                     */
}   pssi_Config_t;

#endif /* PSSI_PSSI_TYPES_H */
