/**
 * \author Mr.Nobody
 * \file Pssi.c
 * \ingroup Pssi
 * \brief Pssi module main functionality (STM32H5)
 *
 * Implements the receive and transmit transfers of the parallel synchronous slave interface,
 * gated by the DE (data enable) and RDY (ready) handshake signals, 8-bit data bus, DMA driven
 * (never ISR/POLL driven in this first version - see the module card and README). PSSI_DR is
 * moved by one Gpdma channel only, software never reads or writes it directly. The Gpdma
 * channel (DmaPeriphId / DmaChannelId / DmaPriority of pssi_Config_t) is configured by the
 * application; Pssi_Get_DefaultConfig proposes GPDMA_PERIPH_1 / GPDMA_CHANNEL_0 / _PRIORITY_LOW.
 *
 * \note The PSSI peripheral exists on the STM32H5 lines whose CMSIS device header defines \c PSSI
 *       (H523, H533, H562, H563, H573, H5E4, H5E5, H5F4, H5F5; not H503, H543, H553). Every
 *       public function checks it (\c defined(PSSI)): on a device without the PSSI it only
 *       returns \ref PSSI_REQUEST_ERROR (\ref Pssi_Task does nothing, \ref Pssi_Get_ModuleVersion
 *       returns the version).
 *
 * \note PSSI and DCMI share the same circuitry (RCC_PERIPH_DCMI_PSSI, the same pins, the same
 *       Gpdma hardware request GPDMA_REQ_DCMI, there is no separate PSSI request line in the
 *       CMSIS / GPDMA request table) and the same NVIC vector (NVIC_PERIPH_IRQ_DCMI_PSSI): a
 *       board uses one of the two peripherals at a time, never both (see the module README).
 *       This module does not enforce that at run time (Dcmi is a separate module, never
 *       touched here); it only documents the constraint.
 *
 * \note This first version implements the 8-bit bus only (EDM=00), the DE/RDY handshake (not
 *       the single-pin bidirectional DE/RDY mode) and a single bounded DMA block per transfer
 *       (no chaining, no circular reception - see the module README "Not part of the first
 *       version").
 *
 */
/* ============================== INCLUDES ================================== */
#include "Pssi.h"                           /* Own header include             */
#include "Pssi_Reg.h"                       /* Register access of the module  */
#include "Rcc_Port.h"                       /* RCC Port module definitions    */
#include "Nvic_Port.h"                      /* NVIC Port module definitions   */
#include "Gpio_Port.h"                      /* GPIO Port module definitions   */
#include "Gpdma_Port.h"                     /* GPDMA Port module definitions  */
#include "Cache_Port.h"                     /* Cache Port module definitions  */
#include "Stm32.h"                          /* PSSI registers definition      */
/* ============================== TYPEDEFS ================================== */

/* ======================== FORWARD DECLARATIONS ============================ */

#if defined(PSSI)

static pssi_RequestState_t PssiPriv_Dma_Init         ( pssi_Direction_t direction );
static pssi_RequestState_t PssiPriv_Dma_Deinit       ( void );
static pssi_RequestState_t PssiPriv_Dma_Start        ( pssi_Direction_t direction,
                                                       const void * const buffer,
                                                       pssi_DataCnt_t byteCnt );
static pssi_RequestState_t PssiPriv_Dma_Stop         ( void );
static pssi_RequestState_t PssiPriv_Set_XferStart    ( pssi_Direction_t direction,
                                                       void * const buffer,
                                                       pssi_DataCnt_t byteCnt );
static pssi_RequestState_t PssiPriv_InitAltFuncGpio  ( gpio_PortId_t portId,
                                                       gpio_PinId_t pinId,
                                                       gpio_AltFunction_t altFunc );

static void                Pssi_Dma_TransferComplete ( void );
static void                Pssi_Dma_Error            ( gpdma_ErrorMaskId_t errorMask );
static void                Pssi_Isr                  ( void );

#endif /* PSSI */

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** \brief Gpdma error bit mask forwarded as \ref PSSI_ERROR_DMA. The suspension (GPDMA_ERROR_SUSPENDED) is left out:
 *         the module suspends the channel itself to stop a transfer (\ref Pssi_Set_Abort, \ref Pssi_Deinit) and the
 *         stop polls the suspension flag, which the interrupt handler would clear first. */
#define PSSI_DMA_ERROR_MASK           ( GPDMA_ERROR_TRANSFER      | \
                                        GPDMA_ERROR_CONFIG_UPDATE | \
                                        GPDMA_ERROR_CONFIG_ERROR  | \
                                        GPDMA_ERROR_TRIG_OVERRUN    )

/** \brief PSSI_CR.DERDYCFG encoding of \ref pssi_Handshake_t (RM0481 34.5.1) */
#define PSSI_CR_DERDYCFG_NONE          ( 0x0u )
#define PSSI_CR_DERDYCFG_RDY           ( 0x1u )
#define PSSI_CR_DERDYCFG_DE            ( 0x2u )
#define PSSI_CR_DERDYCFG_DE_RDY        ( 0x3u )

/** \brief PSSI_CR.EDM encoding of the 8-bit bus (the only width implemented, RM0481 34.5.1) */
#define PSSI_CR_EDM_8BIT               ( 0x0u )

/* =============================== MACROS =================================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

#if defined(PSSI)

/** \brief Run-time state of the single PSSI instance */
static pssi_InstanceData_t     pssi_Instance =
{
    .Initialized   = PSSI_FUNCTION_INACTIVE,
    .XferState      = PSSI_XFER_IDLE,
    .LastError       = 0u,
    .XferByteCnt      = 0u,
    .DmaChannelInit  = PSSI_FUNCTION_INACTIVE,
};

/** \brief Gpdma transfer list of the single transfer configured by this module */
static volatile gpdma_XferList_t    pssi_DmaXferList;

#endif /* PSSI */

/* ========================= EXPORTED FUNCTIONS ============================= */

#if defined(PSSI)

/**
 * \brief Returns a pointer to the single PSSI instance state, for the unit tests only (lets a
 *        test force the instance into an initialized state without replaying the whole
 *        Rcc / Nvic / Gpio expectation chain of \ref Pssi_Init, for a result the test does not
 *        need to re-check). Not declared in Pssi_Port.h, not meant for application use.
 *
 * \param instance [out]: Pointer to the instance pointer to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer.
 */
pssi_RequestState_t PssiPriv_Get_Instance( pssi_InstanceData_t ** const instance )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    if( PSSI_NULL_PTR != instance )
    {
        *instance = &pssi_Instance;
        retState  = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = PSSI_REQUEST_ERROR;
    }

    return ( retState );
}

#endif /* PSSI */


/**
 * \brief Returns the module version.
 *
 * \return Module version.
 */
pssi_ModuleVersion_t Pssi_Get_ModuleVersion( void )
{
    pssi_ModuleVersion_t version = { .Major = 1u, .Minor = 0u, .Patch = 0u };

    return ( version );
}


/**
 * \brief Returns the default configuration: 8-bit bus, falling clock edge, DE/RDY handshake
 *        disabled, active-low polarities, every pin unused, no callback.
 *
 * \param pssiConfig [out]: Pointer to the configuration structure to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer.
 */
pssi_RequestState_t Pssi_Get_DefaultConfig( pssi_Config_t * const pssiConfig )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_NULL_PTR != pssiConfig )
    {
        pssiConfig->DataWidth    = PSSI_DATA_WIDTH_8;
        pssiConfig->ClockEdge     = PSSI_CLOCK_EDGE_FALLING;
        pssiConfig->Handshake     = PSSI_HANDSHAKE_NONE;
        pssiConfig->DePolarity   = PSSI_POLARITY_ACTIVE_LOW;
        pssiConfig->RdyPolarity  = PSSI_POLARITY_ACTIVE_LOW;
        pssiConfig->IrqPriority  = 0u;
        pssiConfig->DmaPeriphId  = GPDMA_PERIPH_1;
        pssiConfig->DmaChannelId = GPDMA_CHANNEL_0;
        pssiConfig->DmaPriority  = GPDMA_PRIORITY_LOW;
        pssiConfig->ClockPin     = PSSI_CLOCK_PIN_UNUSED;
        pssiConfig->DePin         = PSSI_DE_PIN_UNUSED;
        pssiConfig->RdyPin        = PSSI_RDY_PIN_UNUSED;
        for( uint32_t idx = 0u; ( PSSI_DATA_PIN_CNT > idx ); idx++ )
        {
            pssiConfig->DataPin[idx] = PSSI_DATA_PIN_UNUSED;
        }
        pssiConfig->XferCompleteCallback = PSSI_NULL_PTR;
        pssiConfig->ErrorCallback        = PSSI_NULL_PTR;

        retState = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)pssiConfig;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Initializes the PSSI instance: the peripheral clock, the pins, the interrupt priority
 *        and the control register. The transfer direction (OUTEN) is left at its reset value
 *        (receive) and is set again by \ref Pssi_Set_RxStart / \ref Pssi_Set_TxStart; the
 *        interface is left idle (\ref PSSI_XFER_IDLE).
 *
 * \note  Enabling the PSSI clock / pins means the Dcmi module can not be used concurrently on
 *        the same device (shared RCC_PERIPH_DCMI_PSSI, pins and Gpdma request, see the README).
 *
 * \param pssiConfig [in] : Pointer to the configuration structure.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise (the instance is deinitialized).
 */
pssi_RequestState_t Pssi_Init( const pssi_Config_t * const pssiConfig )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    uint32_t              crValue = 0u;

    if( ( PSSI_NULL_PTR         != pssiConfig                ) &&
        ( PSSI_DATA_WIDTH_CNT    > pssiConfig->DataWidth      ) &&
        ( PSSI_CLOCK_EDGE_CNT     > pssiConfig->ClockEdge      ) &&
        ( PSSI_HANDSHAKE_CNT      > pssiConfig->Handshake      ) &&
        ( PSSI_POLARITY_CNT       > pssiConfig->DePolarity     ) &&
        ( PSSI_POLARITY_CNT       > pssiConfig->RdyPolarity    ) &&
        ( PSSI_CLOCK_PIN_UNUSED  != pssiConfig->ClockPin       )    )
    {
        pssi_Instance.Initialized    = PSSI_FUNCTION_ACTIVE;
        pssi_Instance.Config          = *pssiConfig;
        pssi_Instance.XferState        = PSSI_XFER_IDLE;
        pssi_Instance.LastError         = 0u;
        pssi_Instance.XferByteCnt        = 0u;

        (void)Rcc_Set_PeriphActive( RCC_PERIPH_DCMI_PSSI );

        (void)Nvic_Set_PeriphIrq_Handler( NVIC_PERIPH_IRQ_DCMI_PSSI, Pssi_Isr );
        (void)Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, pssiConfig->IrqPriority );

        (void)Pssi_InitClockGpio( pssiConfig->ClockPin );
        if( ( PSSI_HANDSHAKE_DE    == pssiConfig->Handshake ) ||
            ( PSSI_HANDSHAKE_DE_RDY == pssiConfig->Handshake )    )
        {
            (void)Pssi_InitDeGpio( pssiConfig->DePin );
        }
        else
        {
            /* DE not used by the configured handshake, no action required */
        }
        if( ( PSSI_HANDSHAKE_RDY    == pssiConfig->Handshake ) ||
            ( PSSI_HANDSHAKE_DE_RDY == pssiConfig->Handshake )    )
        {
            (void)Pssi_InitRdyGpio( pssiConfig->RdyPin );
        }
        else
        {
            /* RDY not used by the configured handshake, no action required */
        }
        for( uint32_t idx = 0u; ( PSSI_DATA_PIN_CNT > idx ); idx++ )
        {
            if( PSSI_DATA_PIN_UNUSED != pssiConfig->DataPin[idx] )
            {
                (void)Pssi_InitDataGpio( pssiConfig->DataPin[idx] );
            }
            else
            {
                /* Data pin not provided, no action required */
            }
        }

        Pssi_Set_RegVal( PSSI_REG_CR, PSSI_CR_ENABLE, 0u );

        /* pssi_ClockEdge_t / pssi_Polarity_t values are already 0/1 in the same meaning as the
         * matching PSSI_CR bit, so they are cast directly (no ternary operator, see the rules).
         * pssi_Handshake_t has more than two items, it is resolved by PssiDerdyCfg below. */
        uint32_t pssiDerdyCfg = PSSI_CR_DERDYCFG_NONE;

        if( PSSI_HANDSHAKE_RDY == pssiConfig->Handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_RDY;
        }
        else if( PSSI_HANDSHAKE_DE == pssiConfig->Handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_DE;
        }
        else if( PSSI_HANDSHAKE_DE_RDY == pssiConfig->Handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_DE_RDY;
        }
        else
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_NONE;
        }

        crValue = ( ( PSSI_CR_EDM_8BIT                          << PSSI_CR_EDM_Pos      ) |
                    ( (uint32_t)pssiConfig->ClockEdge          << PSSI_CR_CKPOL_Pos     ) |
                    ( (uint32_t)pssiConfig->DePolarity         << PSSI_CR_DEPOL_Pos     ) |
                    ( (uint32_t)pssiConfig->RdyPolarity        << PSSI_CR_RDYPOL_Pos    ) |
                    ( pssiDerdyCfg                               << PSSI_CR_DERDYCFG_Pos  ) |
                    ( (uint32_t)PSSI_DIRECTION_RECEIVE          << PSSI_CR_OUTEN_Pos     ) |
                    ( PSSI_CR_DMAEN                                                        )   );

        Pssi_Set_RegVal( PSSI_REG_CR,
                         ( PSSI_CR_EDM | PSSI_CR_CKPOL | PSSI_CR_DEPOL | PSSI_CR_RDYPOL |
                           PSSI_CR_DERDYCFG | PSSI_CR_OUTEN | PSSI_CR_DMAEN                ),
                         crValue );

        Pssi_Set_RegVal( PSSI_REG_IER, PSSI_IER_OVR_IE, PSSI_IER_OVR_IE );
        (void)Nvic_Set_PeriphIrq_Active( NVIC_PERIPH_IRQ_DCMI_PSSI );

        retState = Pssi_Reg_Set_Enable();

        if( PSSI_REQUEST_OK != retState )
        {
            (void)Pssi_Deinit();
        }
        else
        {
            /* Interface is enabled and idle, no action required */
        }
    }
    else
    {
        /* Invalid parameter, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)pssiConfig;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Deinitializes the PSSI instance: stops a running transfer, disables the interface and
 *        the interrupt, releases the Gpdma channel ownership and disables the peripheral clock.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for an uninitialized instance.
 */
pssi_RequestState_t Pssi_Deinit( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized )
    {
        (void)PssiPriv_Dma_Stop();
        (void)PssiPriv_Dma_Deinit();

        (void)Pssi_Reg_Set_Disable();
        Pssi_Set_RegVal( PSSI_REG_IER, PSSI_IER_OVR_IE, 0u );
        Pssi_Set_RegVal( PSSI_REG_ICR, PSSI_ICR_OVR_ISC, PSSI_ICR_OVR_ISC );

        (void)Nvic_Set_PeriphIrq_Inactive( NVIC_PERIPH_IRQ_DCMI_PSSI );
        (void)Rcc_Set_PeriphInactive( RCC_PERIPH_DCMI_PSSI );

        pssi_Instance.Initialized = PSSI_FUNCTION_INACTIVE;
        pssi_Instance.XferState    = PSSI_XFER_IDLE;

        retState = PSSI_REQUEST_OK;
    }
    else
    {
        /* Instance not initialized, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    /* No peripheral on the device, no action required */
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief The module has no polled work (the transfer is fully interrupt / DMA driven), kept
 *        for interface symmetry with the other Mcal modules.
 */
void Pssi_Task( void )
{
    return;
}


/**
 * \brief Starts a receive transfer: the data sampled on the bus is moved by the Gpdma channel
 *        into \p buffer until \p byteCnt bytes were transferred.
 *
 * \param buffer  [out]: Destination buffer, must be in DMA reachable memory.
 * \param byteCnt [in] : Size of \p buffer in bytes, 1..PSSI_XFER_SIZE_MAX.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise (bad parameter, bad state or
 *         Gpdma failure - the interface stays idle).
 */
pssi_RequestState_t Pssi_Set_RxStart( pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    retState = PssiPriv_Set_XferStart( PSSI_DIRECTION_RECEIVE, buffer, byteCnt );
#else
    (void)buffer;
    (void)byteCnt;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Starts a transmit transfer: \p byteCnt bytes of \p buffer are moved by the Gpdma
 *        channel into the FIFO and output on the data bus.
 *
 * \param buffer  [in]: Source buffer, must be in DMA reachable memory.
 * \param byteCnt [in]: Size of \p buffer in bytes, 1..PSSI_XFER_SIZE_MAX.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise (bad parameter, bad state or
 *         Gpdma failure - the interface stays idle).
 */
pssi_RequestState_t Pssi_Set_TxStart( const pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    /* PssiPriv_Set_XferStart treats the buffer as a plain address (the direction decides
     * whether the Gpdma channel reads or writes it); the const qualifier of the public
     * transmit prototype is dropped here only for that one generic call, the function itself
     * never writes through the pointer in transmit direction. */
    retState = PssiPriv_Set_XferStart( PSSI_DIRECTION_TRANSMIT, (void *)buffer, byteCnt );
#else
    (void)buffer;
    (void)byteCnt;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Stops a running transfer and flushes the FIFO (PSSI_CR.ENABLE cycled off/on).
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Set_Abort( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized )
    {
        retState = PssiPriv_Dma_Stop();

        if( PSSI_REQUEST_OK == retState )
        {
            retState = Pssi_Reg_Set_Disable();
        }
        else
        {
            /* Gpdma channel could not be stopped */
        }

        if( PSSI_REQUEST_OK == retState )
        {
            retState = Pssi_Reg_Set_Enable();
        }
        else
        {
            /* Interface could not be disabled */
        }

        pssi_Instance.XferState = PSSI_XFER_IDLE;
    }
    else
    {
        /* Instance not initialized, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    /* No peripheral on the device, no action required */
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the current transfer state.
 *
 * \param xferState [out]: Pointer to the state to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance (has no side effect, see the module conventions).
 */
pssi_RequestState_t Pssi_Get_XferState( pssi_XferState_t * const xferState )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != xferState                 ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *xferState = pssi_Instance.XferState;
        retState    = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)xferState;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the byte count of the transfer in progress (or of the last one, once it ended).
 *
 * \param transferredCnt [out]: Pointer to the byte count to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance.
 */
pssi_RequestState_t Pssi_Get_XferCount( pssi_DataCnt_t * const transferredCnt )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != transferredCnt            ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *transferredCnt = pssi_Instance.XferByteCnt;
        retState         = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)transferredCnt;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the error mask of the last failed transfer (see PSSI_ERROR_*).
 *
 * \param errorMask [out]: Pointer to the error mask to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance.
 */
pssi_RequestState_t Pssi_Get_XferError( pssi_XferError_t * const errorMask )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != errorMask                 ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *errorMask = pssi_Instance.LastError;
        retState    = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)errorMask;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Changes the parallel bus width while the interface is idle. Only \ref
 *        PSSI_DATA_WIDTH_8 is accepted in this first version (see the module card); PSSI_CR is
 *        briefly disabled and re-enabled (RM0481 34.5.1 note).
 *
 * \param dataWidth [in]: Requested bus width.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Set_DataWidth( pssi_DataWidth_t dataWidth )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized ) &&
        ( PSSI_XFER_IDLE        == pssi_Instance.XferState   ) &&
        ( PSSI_DATA_WIDTH_CNT   > dataWidth                   )    )
    {
        retState = Pssi_Reg_Set_Disable();

        if( PSSI_REQUEST_OK == retState )
        {
            Pssi_Set_RegVal( PSSI_REG_CR, PSSI_CR_EDM, ( PSSI_CR_EDM_8BIT << PSSI_CR_EDM_Pos ) );
            pssi_Instance.Config.DataWidth = dataWidth;
            retState = Pssi_Reg_Set_Enable();
        }
        else
        {
            /* Interface could not be disabled */
        }
    }
    else
    {
        /* Invalid parameter or bad state, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)dataWidth;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the configured parallel bus width.
 *
 * \param dataWidth [out]: Pointer to the bus width to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance.
 */
pssi_RequestState_t Pssi_Get_DataWidth( pssi_DataWidth_t * const dataWidth )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != dataWidth                 ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *dataWidth = pssi_Instance.Config.DataWidth;
        retState    = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)dataWidth;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Changes the active clock edge while the interface is idle (PSSI_CR briefly disabled
 *        and re-enabled, RM0481 34.5.1 note).
 *
 * \param clockEdge [in]: Requested clock edge.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Set_ClockEdge( pssi_ClockEdge_t clockEdge )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized ) &&
        ( PSSI_XFER_IDLE        == pssi_Instance.XferState   ) &&
        ( PSSI_CLOCK_EDGE_CNT   > clockEdge                   )    )
    {
        retState = Pssi_Reg_Set_Disable();

        if( PSSI_REQUEST_OK == retState )
        {
            Pssi_Set_RegVal( PSSI_REG_CR, PSSI_CR_CKPOL, ( (uint32_t)clockEdge << PSSI_CR_CKPOL_Pos ) );
            pssi_Instance.Config.ClockEdge = clockEdge;
            retState = Pssi_Reg_Set_Enable();
        }
        else
        {
            /* Interface could not be disabled */
        }
    }
    else
    {
        /* Invalid parameter or bad state, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)clockEdge;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the configured clock edge.
 *
 * \param clockEdge [out]: Pointer to the clock edge to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance.
 */
pssi_RequestState_t Pssi_Get_ClockEdge( pssi_ClockEdge_t * const clockEdge )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != clockEdge                 ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *clockEdge = pssi_Instance.Config.ClockEdge;
        retState    = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)clockEdge;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Changes the DE / RDY handshake configuration and polarities while the interface is
 *        idle (PSSI_CR briefly disabled and re-enabled, RM0481 34.5.1 note).
 *
 * \param handshake   [in]: Requested handshake use.
 * \param dePolarity  [in]: Requested PSSI_DE active level.
 * \param rdyPolarity [in]: Requested PSSI_RDY active level.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Set_Handshake( pssi_Handshake_t handshake,
                                        pssi_Polarity_t dePolarity,
                                        pssi_Polarity_t rdyPolarity )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized ) &&
        ( PSSI_XFER_IDLE        == pssi_Instance.XferState   ) &&
        ( PSSI_HANDSHAKE_CNT    > handshake                   ) &&
        ( PSSI_POLARITY_CNT     > dePolarity                  ) &&
        ( PSSI_POLARITY_CNT     > rdyPolarity                 )    )
    {
        uint32_t pssiDerdyCfg = PSSI_CR_DERDYCFG_NONE;

        if( PSSI_HANDSHAKE_RDY == handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_RDY;
        }
        else if( PSSI_HANDSHAKE_DE == handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_DE;
        }
        else if( PSSI_HANDSHAKE_DE_RDY == handshake )
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_DE_RDY;
        }
        else
        {
            pssiDerdyCfg = PSSI_CR_DERDYCFG_NONE;
        }

        retState = Pssi_Reg_Set_Disable();

        if( PSSI_REQUEST_OK == retState )
        {
            const uint32_t crValue = ( ( pssiDerdyCfg               << PSSI_CR_DERDYCFG_Pos ) |
                                       ( (uint32_t)dePolarity       << PSSI_CR_DEPOL_Pos    ) |
                                       ( (uint32_t)rdyPolarity      << PSSI_CR_RDYPOL_Pos   )   );

            Pssi_Set_RegVal( PSSI_REG_CR, ( PSSI_CR_DERDYCFG | PSSI_CR_DEPOL | PSSI_CR_RDYPOL ), crValue );

            pssi_Instance.Config.Handshake    = handshake;
            pssi_Instance.Config.DePolarity  = dePolarity;
            pssi_Instance.Config.RdyPolarity = rdyPolarity;

            retState = Pssi_Reg_Set_Enable();
        }
        else
        {
            /* Interface could not be disabled */
        }
    }
    else
    {
        /* Invalid parameter or bad state, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)handshake;
    (void)dePolarity;
    (void)rdyPolarity;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the configured DE / RDY handshake use and polarities.
 *
 * \param handshake   [out]: Pointer to the handshake use to fill.
 * \param dePolarity  [out]: Pointer to the PSSI_DE active level to fill.
 * \param rdyPolarity [out]: Pointer to the PSSI_RDY active level to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR for a null pointer or an uninitialized
 *         instance.
 */
pssi_RequestState_t Pssi_Get_Handshake( pssi_Handshake_t * const handshake,
                                        pssi_Polarity_t * const dePolarity,
                                        pssi_Polarity_t * const rdyPolarity )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != handshake                 ) &&
        ( PSSI_NULL_PTR         != dePolarity                ) &&
        ( PSSI_NULL_PTR         != rdyPolarity               ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        *handshake    = pssi_Instance.Config.Handshake;
        *dePolarity  = pssi_Instance.Config.DePolarity;
        *rdyPolarity = pssi_Instance.Config.RdyPolarity;
        retState      = PSSI_REQUEST_OK;
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)handshake;
    (void)dePolarity;
    (void)rdyPolarity;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Changes the NVIC priority of the shared DCMI/PSSI interrupt.
 *
 * \param irqPriority [in]: Requested priority.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Set_IrqPriority( pssi_IrqPrio_t irqPriority )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized )
    {
        const nvic_RequestState_t nvicState = Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, irqPriority );

        if( NVIC_REQUEST_OK == nvicState )
        {
            pssi_Instance.Config.IrqPriority = irqPriority;
            retState = PSSI_REQUEST_OK;
        }
        else
        {
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Instance not initialized, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)irqPriority;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Reads the NVIC priority of the shared DCMI/PSSI interrupt.
 *
 * \param irqPriority [out]: Pointer to the priority to fill.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_Get_IrqPriority( pssi_IrqPrio_t * const irqPriority )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( ( PSSI_NULL_PTR         != irqPriority                ) &&
        ( PSSI_FUNCTION_ACTIVE  == pssi_Instance.Initialized )    )
    {
        const nvic_RequestState_t nvicState = Nvic_Get_PeriphIrq_Prio( NVIC_PERIPH_IRQ_DCMI_PSSI, irqPriority );

        if( NVIC_REQUEST_OK == nvicState )
        {
            retState = PSSI_REQUEST_OK;
        }
        else
        {
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter or uninitialized instance, no action required */
        retState = PSSI_REQUEST_ERROR;
    }
#else
    (void)irqPriority;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Configures the PSSI_PDCK pin (alternate function, through Gpio).
 *
 * \param clockPin [in]: Pin item.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_InitClockGpio( pssi_ClockPin_t clockPin )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_CLOCK_PIN_UNUSED != clockPin )
    {
        const gpio_PortId_t      port = (gpio_PortId_t)PSSI_BIT_MASK_DECODE_PORT( clockPin );
        const gpio_PinId_t        pin  = (gpio_PinId_t)PSSI_BIT_MASK_DECODE_PIN( clockPin );
        const gpio_AltFunction_t af  = (gpio_AltFunction_t)PSSI_BIT_MASK_DECODE_AF( clockPin );

        retState = PssiPriv_InitAltFuncGpio( port, pin, af );
    }
    else
    {
        /* No pin to configure, no action required */
        retState = PSSI_REQUEST_OK;
    }
#else
    (void)clockPin;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Configures a PSSI_D[7:0] data pin (alternate function, through Gpio).
 *
 * \param dataPin [in]: Pin item.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_InitDataGpio( pssi_DataPin_t dataPin )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_DATA_PIN_UNUSED != dataPin )
    {
        const gpio_PortId_t      port = (gpio_PortId_t)PSSI_BIT_MASK_DECODE_PORT( dataPin );
        const gpio_PinId_t        pin  = (gpio_PinId_t)PSSI_BIT_MASK_DECODE_PIN( dataPin );
        const gpio_AltFunction_t af  = (gpio_AltFunction_t)PSSI_BIT_MASK_DECODE_AF( dataPin );

        retState = PssiPriv_InitAltFuncGpio( port, pin, af );
    }
    else
    {
        /* No pin to configure, no action required */
        retState = PSSI_REQUEST_OK;
    }
#else
    (void)dataPin;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Configures the PSSI_DE pin (alternate function, through Gpio).
 *
 * \param dePin [in]: Pin item.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_InitDeGpio( pssi_DePin_t dePin )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_DE_PIN_UNUSED != dePin )
    {
        const gpio_PortId_t      port = (gpio_PortId_t)PSSI_BIT_MASK_DECODE_PORT( dePin );
        const gpio_PinId_t        pin  = (gpio_PinId_t)PSSI_BIT_MASK_DECODE_PIN( dePin );
        const gpio_AltFunction_t af  = (gpio_AltFunction_t)PSSI_BIT_MASK_DECODE_AF( dePin );

        retState = PssiPriv_InitAltFuncGpio( port, pin, af );
    }
    else
    {
        /* No pin to configure, no action required */
        retState = PSSI_REQUEST_OK;
    }
#else
    (void)dePin;
#endif /* PSSI */

    return ( retState );
}


/**
 * \brief Configures the PSSI_RDY pin (alternate function, through Gpio).
 *
 * \param rdyPin [in]: Pin item.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
pssi_RequestState_t Pssi_InitRdyGpio( pssi_RdyPin_t rdyPin )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

#if defined(PSSI)
    if( PSSI_RDY_PIN_UNUSED != rdyPin )
    {
        const gpio_PortId_t      port = (gpio_PortId_t)PSSI_BIT_MASK_DECODE_PORT( rdyPin );
        const gpio_PinId_t        pin  = (gpio_PinId_t)PSSI_BIT_MASK_DECODE_PIN( rdyPin );
        const gpio_AltFunction_t af  = (gpio_AltFunction_t)PSSI_BIT_MASK_DECODE_AF( rdyPin );

        retState = PssiPriv_InitAltFuncGpio( port, pin, af );
    }
    else
    {
        /* No pin to configure, no action required */
        retState = PSSI_REQUEST_OK;
    }
#else
    (void)rdyPin;
#endif /* PSSI */

    return ( retState );
}

/* ========================= LOCAL (PssiPriv) FUNCTIONS ======================== */

#if defined(PSSI)

/**
 * \brief Configures one GPIO pin as a PSSI input/output (alternate function, no pull - the
 *        external device drives the clock, DE and RDY are driven per the handshake direction).
 *
 * \param portId  [in] : Port of the pin.
 * \param pinId   [in] : Pin identification.
 * \param altFunc [in] : Alternate function of the pin.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR when the Gpio module rejects a step.
 */
static pssi_RequestState_t PssiPriv_InitAltFuncGpio( gpio_PortId_t portId,
                                                     gpio_PinId_t pinId,
                                                     gpio_AltFunction_t altFunc )
{
    pssi_RequestState_t retState  = PSSI_REQUEST_ERROR;
    gpio_Config_t          gpioConfig;
    gpio_RequestState_t     gpioState = GPIO_REQUEST_ERROR;

    gpioConfig.PortId         = portId;
    gpioConfig.PinId          = pinId;
    gpioConfig.PinMode        = GPIO_PIN_MODE_ALTERNATE;
    gpioConfig.PinPull        = GPIO_PIN_PULL_NONE;
    gpioConfig.PinSpeed       = GPIO_PIN_SPEED_VERY_HIGH;
    gpioConfig.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
    gpioConfig.PinAltFunction = altFunc;
    gpioConfig.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;
    gpioState                 = Gpio_Init( &gpioConfig );

    if( GPIO_REQUEST_OK == gpioState )
    {
        retState = PSSI_REQUEST_OK;
    }
    else
    {
        /* Gpio rejected the configuration, no action required */
        retState = PSSI_REQUEST_ERROR;
    }

    return ( retState );
}

/**
 * \brief Common body of \ref Pssi_Set_RxStart / \ref Pssi_Set_TxStart: validates the
 *        parameters and the state, sets OUTEN for \p direction (PSSI_CR briefly disabled and
 *        re-enabled, RM0481 34.5.1 note), then starts the Gpdma transfer.
 *
 * \param direction [in]    : Requested transfer direction.
 * \param buffer    [in/out]: Source (transmit) or destination (receive) buffer.
 * \param byteCnt   [in]    : Size of \p buffer in bytes.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
static pssi_RequestState_t PssiPriv_Set_XferStart( pssi_Direction_t direction,
                                                   void * const buffer,
                                                   pssi_DataCnt_t byteCnt )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    if( ( PSSI_FUNCTION_ACTIVE == pssi_Instance.Initialized ) &&
        ( PSSI_XFER_IDLE        == pssi_Instance.XferState   ) &&
        ( PSSI_NULL_PTR         != buffer                     ) &&
        ( 0u                     < byteCnt                    ) &&
        ( PSSI_XFER_SIZE_MAX    >= byteCnt                    )    )
    {
        retState = Pssi_Reg_Set_Disable();

        if( PSSI_REQUEST_OK == retState )
        {
            Pssi_Set_RegVal( PSSI_REG_CR, PSSI_CR_OUTEN, ( (uint32_t)direction << PSSI_CR_OUTEN_Pos ) );
            retState = Pssi_Reg_Set_Enable();
        }
        else
        {
            /* Interface could not be disabled */
        }

        if( PSSI_REQUEST_OK == retState )
        {
            retState = PssiPriv_Dma_Start( direction, buffer, byteCnt );
        }
        else
        {
            /* OUTEN could not be set, no Gpdma transfer is started */
        }

        if( PSSI_REQUEST_OK == retState )
        {
            pssi_Instance.XferByteCnt = byteCnt;
            if( PSSI_DIRECTION_RECEIVE == direction )
            {
                pssi_Instance.XferState = PSSI_XFER_RECEIVING;
            }
            else
            {
                pssi_Instance.XferState = PSSI_XFER_TRANSMITTING;
            }
        }
        else
        {
            /* Transfer could not be started, the interface stays idle */
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter or bad state, no action required */
        retState = PSSI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Initializes (or reuses) the Gpdma channel for \p direction and enables its error
 *        interrupt. The channel is reinitialized at every call where \p direction differs from
 *        the direction configured at the previous call (half duplex: never both at once).
 *
 * \param direction [in]: Requested transfer direction.
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
static pssi_RequestState_t PssiPriv_Dma_Init( pssi_Direction_t direction )
{
    pssi_RequestState_t    retState   = PSSI_REQUEST_ERROR;
    gpdma_ConfigStruct_t     dmaConfig  = { 0 };
    gpdma_TransferConfig_t   xferConfig = { 0 };
    gpdma_RequestState_t     dmaState   = GPDMA_REQUEST_ERROR;

    if( PSSI_DIRECTION_RECEIVE == direction )
    {
        xferConfig.Direction       = GPDMA_DIR_PERIPH_TO_MEMORY;
        xferConfig.SourceAddr       = (gpdma_SrcAddr_t)Pssi_Reg_Get_DataRegAddr();
        xferConfig.SourceAddrMode  = GPDMA_ADDR_STATIC;
        xferConfig.DestinationAddrMode = GPDMA_ADDR_INCREMENT;
    }
    else
    {
        xferConfig.Direction       = GPDMA_DIR_MEMORY_TO_PERIPH;
        xferConfig.DestinationAddr  = (gpdma_DstAddr_t)Pssi_Reg_Get_DataRegAddr();
        xferConfig.DestinationAddrMode = GPDMA_ADDR_STATIC;
        xferConfig.SourceAddrMode  = GPDMA_ADDR_INCREMENT;
    }
    xferConfig.EventMode              = GPDMA_TRANSFER_EVENT_BLOCK;
    xferConfig.TriggerType            = GPDMA_TRG_NOT_USED;
    xferConfig.TriggerMode            = GPDMA_TRIGGER_BLOCK;
    xferConfig.RequestSource          = GPDMA_REQ_DCMI; /* PSSI shares the DCMI Gpdma request, see the README */
    xferConfig.RequestMode            = GPDMA_PERIPH_REQ_SINGLE;
    xferConfig.BlockSize              = 0u; /* Set per transfer by PssiPriv_Dma_Start */
    xferConfig.BlockRepetitionCount   = 0u;
    xferConfig.SourceDataSize         = GPDMA_DATA_SIZE_8BITS;
    xferConfig.SourceBurstLength      = 1u;
    xferConfig.SourcePortId           = GPDMA_PORT_DEFAULT;
    xferConfig.SourceDataOp           = GPDMA_SRC_DATA_PRESERVE;
    xferConfig.SourceBlockOffset2D    = 0u;
    xferConfig.SourceRepBlockOffset2D = 0u;
    xferConfig.DestinationDataSize    = GPDMA_DATA_SIZE_8BITS;
    xferConfig.DestinationBurstLength = 1u;
    xferConfig.DestinationPortId      = GPDMA_PORT_DEFAULT;
    xferConfig.DestinationDataOp      = GPDMA_DEST_DATA_PRESERVE;
    xferConfig.DestinationBlockOffset2D    = 0u;
    xferConfig.DestinationRepBlockOffset2D = 0u;

    dmaState = Gpdma_Get_DefaultConfig( &dmaConfig );

    dmaConfig.PeriphId            = (gpdma_PeriphId_t)pssi_Instance.Config.DmaPeriphId;
    dmaConfig.ChannelId           = (gpdma_ChannelId_t)pssi_Instance.Config.DmaChannelId;
    dmaConfig.ChannelPrio         = (gpdma_Priority_t)pssi_Instance.Config.DmaPriority;
    dmaConfig.TransferExecMode    = GPDMA_XFER_EXEC_BLOCKING;
    dmaConfig.TransferConfig      = &xferConfig;
    dmaConfig.TransfersCount      = 1u;
    dmaConfig.XferListAccessMode  = GPDMA_TRANSFER_LIST_ACCESS_SINGLE;
    dmaConfig.XferList            = &pssi_DmaXferList;
    dmaConfig.TransferLockState    = GPDMA_TRANSFER_LIST_LOCKED;
    dmaConfig.TransferCompleteIsr = Pssi_Dma_TransferComplete;
    dmaConfig.HalfTransferIsr     = PSSI_NULL_PTR;
    dmaConfig.ErrorIsr            = Pssi_Dma_Error;
    dmaConfig.ErrorMask           = PSSI_DMA_ERROR_MASK;

    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.DmaChannelInit )
    {
        (void)PssiPriv_Dma_Deinit();
    }
    else
    {
        /* No previous channel to release, no action required */
    }

    if( GPDMA_REQUEST_OK == dmaState )
    {
        dmaState = Gpdma_Init( &dmaConfig );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            pssi_Instance.DmaChannelInit = PSSI_FUNCTION_ACTIVE;
        }
        else
        {
            /* Gpdma channel initialization failed */
        }
    }
    else
    {
        /* Default configuration is not available */
    }

    if( GPDMA_REQUEST_OK == dmaState )
    {
        dmaState = Gpdma_Set_InterruptActive( dmaConfig.PeriphId, dmaConfig.ChannelId );
    }
    else
    {
        /* Gpdma channel configuration failed */
    }

    if( GPDMA_REQUEST_OK == dmaState )
    {
        retState = PSSI_REQUEST_OK;
    }
    else
    {
        retState = PSSI_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Disables the Gpdma channel of the transfer (channel configuration is dropped, the next
 *        start reinitializes it, see \ref PssiPriv_Dma_Init).
 *
 * \return PSSI_REQUEST_OK on success (also if no channel was initialized), PSSI_REQUEST_ERROR
 *         otherwise.
 */
static pssi_RequestState_t PssiPriv_Dma_Deinit( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.DmaChannelInit )
    {
        const gpdma_PeriphId_t  dmaPeriph = (gpdma_PeriphId_t)pssi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t dmaChan   = (gpdma_ChannelId_t)pssi_Instance.Config.DmaChannelId;
        gpdma_RequestState_t     dmaState  = Gpdma_Set_ChannelInactive( dmaPeriph, dmaChan );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Set_InterruptInactive( dmaPeriph, dmaChan );
        }
        else
        {
            /* Channel could not be disabled */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Deinit( dmaPeriph, dmaChan );
        }
        else
        {
            /* Interrupt could not be disabled */
        }

        pssi_Instance.DmaChannelInit = PSSI_FUNCTION_INACTIVE;

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = PSSI_REQUEST_OK;
        }
        else
        {
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* No Gpdma channel was initialized */
        retState = PSSI_REQUEST_OK;
    }

    return ( retState );
}


/**
 * \brief Initializes the Gpdma channel for \p direction, arms it with \p buffer / \p byteCnt
 *        and activates it. Cleans (transmit) or invalidates (receive) the data cache range of
 *        \p buffer first, so the DMA and the CPU observe the same memory (\ref Cache_Port.h).
 *
 * \param direction [in]: Requested transfer direction.
 * \param buffer    [in]: Source (transmit) or destination (receive) buffer.
 * \param byteCnt   [in]: Size of \p buffer in bytes (already checked by the caller).
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
static pssi_RequestState_t PssiPriv_Dma_Start( pssi_Direction_t direction,
                                               const void * const buffer,
                                               pssi_DataCnt_t byteCnt )
{
    pssi_RequestState_t retState = PssiPriv_Dma_Init( direction );

    if( PSSI_REQUEST_OK == retState )
    {
        const gpdma_PeriphId_t  dmaPeriph = (gpdma_PeriphId_t)pssi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t dmaChan   = (gpdma_ChannelId_t)pssi_Instance.Config.DmaChannelId;
        gpdma_RequestState_t     dmaState  = GPDMA_REQUEST_ERROR;

        dmaState = Gpdma_Set_BlockSize( dmaPeriph, dmaChan, (gpdma_BlockSize_t)byteCnt );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            if( PSSI_DIRECTION_RECEIVE == direction )
            {
                dmaState = Gpdma_Set_DestinationAddr( dmaPeriph, dmaChan, (gpdma_DstAddr_t)buffer );
                (void)Cache_Set_InvalidateRange( CACHE_DATA, (uint32_t)buffer, byteCnt );
            }
            else
            {
                dmaState = Gpdma_Set_SourceAddr( dmaPeriph, dmaChan, (gpdma_SrcAddr_t)buffer );
                (void)Cache_Set_CleanRange( CACHE_DATA, (uint32_t)buffer, byteCnt );
            }
        }
        else
        {
            /* Block size could not be set */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            dmaState = Gpdma_Set_ChannelActive( dmaPeriph, dmaChan );
        }
        else
        {
            /* Transfer address could not be set */
        }

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = PSSI_REQUEST_OK;
        }
        else
        {
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* Gpdma channel could not be initialized */
    }

    return ( retState );
}


/**
 * \brief Disables the Gpdma channel (keeps its configuration until the next \ref
 *        PssiPriv_Dma_Init).
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
static pssi_RequestState_t PssiPriv_Dma_Stop( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    if( PSSI_FUNCTION_ACTIVE == pssi_Instance.DmaChannelInit )
    {
        const gpdma_PeriphId_t     dmaPeriph = (gpdma_PeriphId_t)pssi_Instance.Config.DmaPeriphId;
        const gpdma_ChannelId_t    dmaChan   = (gpdma_ChannelId_t)pssi_Instance.Config.DmaChannelId;
        const gpdma_RequestState_t dmaState  = Gpdma_Set_ChannelInactive( dmaPeriph, dmaChan );

        if( GPDMA_REQUEST_OK == dmaState )
        {
            retState = PSSI_REQUEST_OK;
        }
        else
        {
            retState = PSSI_REQUEST_ERROR;
        }
    }
    else
    {
        /* No Gpdma channel was initialized, no action required */
        retState = PSSI_REQUEST_OK;
    }

    return ( retState );
}


/**
 * \brief Gpdma transfer complete handler of the transfer channel: the requested byte count was
 *        moved. Reports the transfer to the application and brings the interface back to idle.
 */
static void Pssi_Dma_TransferComplete( void )
{
    pssi_Instance.XferState = PSSI_XFER_IDLE;

    if( PSSI_NULL_PTR != pssi_Instance.Config.XferCompleteCallback )
    {
        pssi_Instance.Config.XferCompleteCallback( pssi_Instance.XferByteCnt );
    }
    else
    {
        /* No callback registered, no action required */
    }
}


/**
 * \brief Gpdma error handler of the transfer channel, forwarded as \ref PSSI_ERROR_DMA.
 *
 * \param errorMask [in] : Gpdma error bit mask (not forwarded as is, only the module's own
 *                          PSSI_ERROR_DMA bit is reported - see pssi_XferError_t).
 */
static void Pssi_Dma_Error( gpdma_ErrorMaskId_t errorMask )
{
    (void)errorMask;

    pssi_Instance.XferState  = PSSI_XFER_ERROR;
    pssi_Instance.LastError |= PSSI_ERROR_DMA;

    if( PSSI_NULL_PTR != pssi_Instance.Config.ErrorCallback )
    {
        pssi_Instance.Config.ErrorCallback( pssi_Instance.LastError );
    }
    else
    {
        /* No callback registered, no action required */
    }
}


/**
 * \brief DCMI/PSSI shared interrupt handler. The PSSI generates only one interrupt source
 *        (OVR_RIS, RM0481 34.4): an overrun in receive mode or an underrun in transmit mode.
 */
static void Pssi_Isr( void )
{
    const uint32_t rawStatus = Pssi_Get_RegVal( PSSI_REG_RIS, PSSI_RIS_OVR_RIS );

    if( 0u != rawStatus )
    {
        Pssi_Set_RegVal( PSSI_REG_ICR, PSSI_ICR_OVR_ISC, PSSI_ICR_OVR_ISC );

        /* The hardware reports a single combined bit (OVR_RIS): an overrun in receive mode or
         * an underrun in transmit mode (RM0481 34.3.5). The current PSSI_CR.OUTEN bit tells
         * which of the two applies. */
        const uint32_t outenBit = Pssi_Get_RegBit( PSSI_REG_CR, PSSI_CR_OUTEN );

        if( 0u != outenBit )
        {
            pssi_Instance.LastError |= PSSI_ERROR_UNDERRUN;
        }
        else
        {
            pssi_Instance.LastError |= PSSI_ERROR_OVERRUN;
        }

        pssi_Instance.XferState = PSSI_XFER_ERROR;

        if( PSSI_NULL_PTR != pssi_Instance.Config.ErrorCallback )
        {
            pssi_Instance.Config.ErrorCallback( pssi_Instance.LastError );
        }
        else
        {
            /* No callback registered, no action required */
        }
    }
    else
    {
        /* No overrun/underrun event, no action required */
    }
}

#endif /* PSSI */
