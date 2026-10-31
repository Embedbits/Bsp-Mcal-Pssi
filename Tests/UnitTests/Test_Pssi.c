/**
 * \author Mr.Nobody
 * \file Test_Pssi.c
 * \ingroup Pssi
 * \brief Unit tests of the Pssi module (STM32H5).
 *
 * Pssi.c and Pssi_Reg.c are compiled unchanged with the real CMSIS registers emulated by
 * RegMem (plain host memory); Rcc, Nvic, Gpio, Gpdma and Cache are mocked by CMock. On the
 * lines without a PSSI peripheral (H503/H543/H553) every public function checks the CMSIS
 * definition PSSI and only returns PSSI_REQUEST_ERROR - the tests of this file compile and run
 * on all 12 lines, every PSSI-specific assertion is guarded by `#if defined(PSSI)`.
 *
 * \note  Emulated registers are plain RAM: PSSI_RIS (hardware set, read only) and the write-only
 *        PSSI_ICR (set-to-clear) are not modeled with their real hardware behaviour, a test that
 *        needs a raw interrupt flag would have to preset PSSI->RIS directly. PSSI_CR.ENABLE
 *        read-back loops (Pssi_Reg_Set_Enable / Pssi_Reg_Set_Disable) succeed immediately because
 *        the write already lands in the same plain memory cell.
 *
 * \note  Pssi_Isr, Pssi_Dma_TransferComplete and Pssi_Dma_Error are `static` (not reachable from
 *        this file) and are registered only as function pointers passed to Nvic / Gpdma mocks
 *        that do not call back into them; they are exercised on the target by the integration
 *        tests only (not run on hardware, see the module README) - same precedent as Dcmi.
 *
 * \note  PssiPriv_Get_Instance() is used by several tests to put the single instance into an
 *        initialized state without calling Pssi_Init (which would need a longer, strictly
 *        ordered chain of Rcc / Nvic / Gpio mock expectations for a result these tests do not
 *        need to re-check) - see its own Doxygen comment in Pssi.c for why it exists.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "RegMem.h"                         /* Register memory emulation      */
#include "Pssi_Port.h"                      /* Module under test (public)     */
#include "Pssi.h"                           /* Module under test (private)    */
#include "Pssi_Reg.h"                       /* Register access (private)      */
#include "MockRcc_Port.h"                   /* RCC module mock                */
#include "MockNvic_Port.h"                  /* NVIC module mock                */
#include "MockGpio_Port.h"                  /* GPIO module mock                */
#include "MockGpdma_Port.h"                 /* GPDMA module mock               */
#include "MockCache_Port.h"                 /* Cache module mock               */
#include "Stm32.h"                          /* PSSI registers definition      */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

#if defined(PSSI)
static pssi_Config_t   UtPssi_Get_MinimalConfig        ( void );
static void              UtPssi_Force_Initialized       ( pssi_XferState_t xferState );
#endif /* PSSI */

/* ========================== SYMBOLIC CONSTANTS ============================= */

/** NVIC priority used by the tests */
#define UT_PSSI_IRQ_PRIO                     ( 5u )

/** Transfer buffer size used by the start tests */
#define UT_PSSI_BUFFER_SIZE_BYTE             ( 64u )

/** Value of the output parameters of the peripheral check test, proves that they stay unchanged */
#define UT_PSSI_UNTOUCHED_VALUE               ( 77u )

/** PSSI_CR.DERDYCFG encoded value of PSSI_HANDSHAKE_DE_RDY (RM0481 34.5.1), used to check the
 *  register directly without duplicating the module's own encoding table in the test */
#define UT_PSSI_CR_DERDYCFG_DE_RDY            ( 3u << PSSI_CR_DERDYCFG_Pos )

/* ============================ TEST FIXTURE ================================ */

void setUp( void )
{
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Reset() );
}


void tearDown( void )
{
    /* No mock to verify beyond the ordered expectations of each test */
}

/* ========================== MODULE VERSION ================================ */

/**
 * \brief   Pssi_Get_ModuleVersion() returns the version of the module.
 *
 * \par Expected results
 * - Version is 1.0.0.
 */
void Ut_Pssi_Get_ModuleVersion_Call_ReturnsVersion( void )
{
    pssi_ModuleVersion_t version = Pssi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
    TEST_ASSERT_EQUAL_UINT8( 0u, version.Minor );
    TEST_ASSERT_EQUAL_UINT8( 0u, version.Patch );
}

/* ========================= DEFAULT CONFIGURATION =========================== */

/**
 * \brief   Pssi_Get_DefaultConfig() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_DefaultConfig_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DefaultConfig( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Get_DefaultConfig() fills a safe configuration (8-bit bus, falling edge, no
 *          handshake, no pin, no callback).
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - DataWidth is 8-bit, ClockEdge is falling, Handshake is none.
 * - Every pin and every callback is the "unused" / null item.
 */
void Ut_Pssi_Get_DefaultConfig_Call_FillsSafeDefaults( void )
{
    pssi_Config_t config;

#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_DATA_WIDTH_8, config.DataWidth );
    TEST_ASSERT_EQUAL( PSSI_CLOCK_EDGE_FALLING, config.ClockEdge );
    TEST_ASSERT_EQUAL( PSSI_HANDSHAKE_NONE, config.Handshake );
    TEST_ASSERT_EQUAL( GPDMA_PERIPH_1, config.DmaPeriphId );
    TEST_ASSERT_EQUAL( GPDMA_CHANNEL_0, config.DmaChannelId );
    TEST_ASSERT_EQUAL( GPDMA_PRIORITY_LOW, config.DmaPriority );
    TEST_ASSERT_EQUAL( PSSI_CLOCK_PIN_UNUSED, config.ClockPin );
    TEST_ASSERT_EQUAL( PSSI_DE_PIN_UNUSED, config.DePin );
    TEST_ASSERT_EQUAL( PSSI_RDY_PIN_UNUSED, config.RdyPin );
    TEST_ASSERT_EQUAL( PSSI_DATA_PIN_UNUSED, config.DataPin[0] );
    TEST_ASSERT_EQUAL( PSSI_DATA_PIN_UNUSED, config.DataPin[PSSI_DATA_PIN_CNT - 1u] );
    TEST_ASSERT_NULL( (void *)config.XferCompleteCallback );
    TEST_ASSERT_NULL( (void *)config.ErrorCallback );
#else
    /* No PSSI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DefaultConfig( &config ) );
#endif /* PSSI */
}

/* ============================= INITIALIZATION ================================ */

/**
 * \brief   Pssi_Init() rejects a null configuration pointer (no register and no other module is
 *          touched, so this test needs no mock expectation).
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Init_NullConfig_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Init( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Init() rejects a configuration with no clock pin configured.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Init_ClockPinUnused_ReturnsError( void )
{
    pssi_Config_t config;

#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_DefaultConfig( &config ) );
#else
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DefaultConfig( &config ) );
#endif /* PSSI */

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Init( &config ) );
}


/**
 * \brief   Pssi_Init() with a minimal configuration (no handshake, so no DE / RDY pin, no data
 *          pin) enables the peripheral and configures PSSI_CR (receive direction, DMA enabled).
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - PSSI_CR.ENABLE and PSSI_CR.DMAEN are set, PSSI_CR.OUTEN and PSSI_CR.DERDYCFG are clear.
 */
void Ut_Pssi_Init_Minimal_EnablesPeripheral( void )
{
#if defined(PSSI)
    pssi_Config_t config = UtPssi_Get_MinimalConfig();

    Rcc_Set_PeriphActive_ExpectAndReturn( RCC_PERIPH_DCMI_PSSI, RCC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Handler_ExpectAnyArgsAndReturn( NVIC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, UT_PSSI_IRQ_PRIO, NVIC_REQUEST_OK );
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );
    Nvic_Set_PeriphIrq_Active_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_ENABLE, PSSI->CR & PSSI_CR_ENABLE );
    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_DMAEN, PSSI->CR & PSSI_CR_DMAEN );
    TEST_ASSERT_EQUAL_HEX32( 0u, PSSI->CR & PSSI_CR_OUTEN );
    TEST_ASSERT_EQUAL_HEX32( 0u, PSSI->CR & PSSI_CR_DERDYCFG );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Init() with the DE/RDY handshake configures PSSI_CR.DERDYCFG to the both-enabled
 *          value (3) and configures the DE and RDY pins (one extra Gpio_Init call each, on top
 *          of the clock pin).
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - PSSI_CR.DERDYCFG is 3 (PSSI_HANDSHAKE_DE_RDY).
 */
void Ut_Pssi_Init_DeRdyHandshake_ConfiguresDerdyCfg( void )
{
#if defined(PSSI)
    pssi_Config_t config = UtPssi_Get_MinimalConfig();

    config.Handshake = PSSI_HANDSHAKE_DE_RDY;
    config.DePin      = PSSI_DE_PIN_PA4;
    config.RdyPin     = PSSI_RDY_PIN_PB7;

    Rcc_Set_PeriphActive_ExpectAndReturn( RCC_PERIPH_DCMI_PSSI, RCC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Handler_ExpectAnyArgsAndReturn( NVIC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, UT_PSSI_IRQ_PRIO, NVIC_REQUEST_OK );
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK ); /* Clock pin */
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK ); /* DE pin */
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK ); /* RDY pin */
    Nvic_Set_PeriphIrq_Active_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );

    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_DERDYCFG & UT_PSSI_CR_DERDYCFG_DE_RDY,
                              PSSI->CR & PSSI_CR_DERDYCFG );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Deinit() on an instance that was never initialized is rejected.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Deinit_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Deinit() );
}


/**
 * \brief   Pssi_Deinit() on an instance forced into the initialized state (no Gpdma channel was
 *          ever created) disables the peripheral and releases Rcc / Nvic.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - PSSI_CR.ENABLE is cleared.
 */
void Ut_Pssi_Deinit_Initialized_DisablesPeripheral( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );
    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    Nvic_Set_PeriphIrq_Inactive_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, NVIC_REQUEST_OK );
    Rcc_Set_PeriphInactive_ExpectAndReturn( RCC_PERIPH_DCMI_PSSI, RCC_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Deinit() );
    TEST_ASSERT_EQUAL_HEX32( 0u, PSSI->CR & PSSI_CR_ENABLE );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Task() never fails (no polled work exists, the module is fully DMA driven).
 */
void Ut_Pssi_Task_Call_DoesNotCrash( void )
{
    Pssi_Task();
}

/* ============================== TRANSFER START ================================ */

/**
 * \brief   Pssi_Set_RxStart() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_RxStart_NotInitialized_ReturnsError( void )
{
    pssi_Data_t buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
}


/**
 * \brief   Pssi_Set_RxStart() rejects a null buffer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_RxStart_NullBuffer_ReturnsError( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( PSSI_NULL_PTR, UT_PSSI_BUFFER_SIZE_BYTE ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_RxStart() rejects a zero byte count.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_RxStart_ZeroByteCnt_ReturnsError( void )
{
#if defined(PSSI)
    pssi_Data_t buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( buffer, 0u ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_RxStart() rejects a byte count above PSSI_XFER_SIZE_MAX (mutation check: the
 *          boundary value itself, PSSI_XFER_SIZE_MAX, must still be accepted - see the next
 *          test - only PSSI_XFER_SIZE_MAX + 1 is rejected).
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_RxStart_OversizedByteCnt_ReturnsError( void )
{
#if defined(PSSI)
    pssi_Data_t buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( buffer, PSSI_XFER_SIZE_MAX + 1u ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_RxStart() rejects the request while a transfer is already running.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_RxStart_AlreadyRunning_ReturnsError( void )
{
#if defined(PSSI)
    pssi_Data_t buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];

    UtPssi_Force_Initialized( PSSI_XFER_RECEIVING );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_RxStart() on an idle, initialized instance sets PSSI_CR.OUTEN to receive (0),
 *          initializes and arms the Gpdma channel and invalidates the destination cache range.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - Pssi_Get_XferState() reports PSSI_XFER_RECEIVING.
 * - Pssi_Get_XferCount() reports the requested byte count.
 */
void Ut_Pssi_Set_RxStart_Valid_StartsDmaAndSetsReceiving( void )
{
#if defined(PSSI)
    pssi_Data_t         buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];
    pssi_XferState_t     xferState     = PSSI_XFER_IDLE;
    pssi_DataCnt_t        transferredCnt = 0u;
    gpdma_ConfigStruct_t  dmaConfig;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    Gpdma_Get_DefaultConfig_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Get_DefaultConfig_ReturnThruPtr_configStruct( &dmaConfig );
    Gpdma_Init_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_InterruptActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_BlockSize_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_DestinationAddr_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Cache_Set_InvalidateRange_ExpectAnyArgsAndReturn( CACHE_REQUEST_OK );
    Gpdma_Set_ChannelActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_RxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL_HEX32( 0u, PSSI->CR & PSSI_CR_OUTEN );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_RECEIVING, xferState );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferCount( &transferredCnt ) );
    TEST_ASSERT_EQUAL_UINT32( UT_PSSI_BUFFER_SIZE_BYTE, transferredCnt );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_TxStart() on an idle, initialized instance sets PSSI_CR.OUTEN to transmit (1),
 *          initializes and arms the Gpdma channel and cleans the source cache range.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - Pssi_Get_XferState() reports PSSI_XFER_TRANSMITTING.
 */
void Ut_Pssi_Set_TxStart_Valid_StartsDmaAndSetsTransmitting( void )
{
#if defined(PSSI)
    const pssi_Data_t    buffer[ UT_PSSI_BUFFER_SIZE_BYTE ] = { 0 };
    pssi_XferState_t      xferState = PSSI_XFER_IDLE;
    gpdma_ConfigStruct_t   dmaConfig;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    Gpdma_Get_DefaultConfig_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Get_DefaultConfig_ReturnThruPtr_configStruct( &dmaConfig );
    Gpdma_Init_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_InterruptActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_BlockSize_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Gpdma_Set_SourceAddr_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );
    Cache_Set_CleanRange_ExpectAnyArgsAndReturn( CACHE_REQUEST_OK );
    Gpdma_Set_ChannelActive_ExpectAnyArgsAndReturn( GPDMA_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_TxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_OUTEN, PSSI->CR & PSSI_CR_OUTEN );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_TRANSMITTING, xferState );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}

/* ================================== ABORT ===================================== */

/**
 * \brief   Pssi_Set_Abort() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_Abort_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_Abort() );
}


/**
 * \brief   Pssi_Set_Abort() on an initialized instance (no Gpdma channel was ever created) cycles
 *          PSSI_CR.ENABLE off and on and brings the state back to idle.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - Pssi_Get_XferState() reports PSSI_XFER_IDLE.
 */
void Ut_Pssi_Set_Abort_Initialized_ReturnsIdle( void )
{
#if defined(PSSI)
    pssi_XferState_t xferState = PSSI_XFER_RECEIVING;

    UtPssi_Force_Initialized( PSSI_XFER_RECEIVING );
    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_Abort() );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_IDLE, xferState );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}

/* ============================ TRANSFER STATE / COUNT / ERROR =================== */

/**
 * \brief   Pssi_Get_XferState() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_XferState_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferState( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Get_XferCount() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_XferCount_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferCount( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Get_XferError() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_XferError_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferError( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Get_XferError() on an initialized instance reports the error mask stored by a
 *          previous failed transfer (forced directly, see the file header note on the static
 *          ISR / Gpdma callbacks).
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 * - The reported mask is PSSI_ERROR_OVERRUN.
 */
void Ut_Pssi_Get_XferError_Initialized_ReturnsStoredError( void )
{
#if defined(PSSI)
    pssi_InstanceData_t * instance   = PSSI_NULL_PTR;
    pssi_XferError_t        errorMask = 0u;

    UtPssi_Force_Initialized( PSSI_XFER_ERROR );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, PssiPriv_Get_Instance( &instance ) );
    instance->LastError = PSSI_ERROR_OVERRUN;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferError( &errorMask ) );
    TEST_ASSERT_EQUAL_HEX32( PSSI_ERROR_OVERRUN, errorMask );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}

/* ================================ SETTINGS ===================================== */

/**
 * \brief   Pssi_Set_DataWidth() rejects an out-of-range value.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_DataWidth_InvalidValue_ReturnsError( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_DataWidth( PSSI_DATA_WIDTH_CNT ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_DataWidth() rejects the request while a transfer is running (mutation check:
 *          the idle boundary - see the next test, which uses the same value while idle and
 *          succeeds).
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_DataWidth_NotIdle_ReturnsError( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_RECEIVING );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_DataWidth( PSSI_DATA_WIDTH_8 ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_DataWidth() on an idle instance accepts PSSI_DATA_WIDTH_8 and Pssi_Get_DataWidth()
 *          reports it back.
 *
 * \par Expected results
 * - Both calls return PSSI_REQUEST_OK.
 * - The reported width is PSSI_DATA_WIDTH_8.
 */
void Ut_Pssi_Set_DataWidth_Valid_SetsWidth( void )
{
#if defined(PSSI)
    pssi_DataWidth_t dataWidth = PSSI_DATA_WIDTH_CNT;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );
    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_DataWidth( PSSI_DATA_WIDTH_8 ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_DataWidth( &dataWidth ) );
    TEST_ASSERT_EQUAL( PSSI_DATA_WIDTH_8, dataWidth );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Get_DataWidth() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_DataWidth_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DataWidth( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Set_ClockEdge() on an idle instance accepts PSSI_CLOCK_EDGE_RISING and sets
 *          PSSI_CR.CKPOL; Pssi_Get_ClockEdge() reports it back.
 *
 * \par Expected results
 * - Both calls return PSSI_REQUEST_OK.
 * - PSSI_CR.CKPOL is set, the reported edge is PSSI_CLOCK_EDGE_RISING.
 */
void Ut_Pssi_Set_ClockEdge_Valid_SetsEdge( void )
{
#if defined(PSSI)
    pssi_ClockEdge_t clockEdge = PSSI_CLOCK_EDGE_CNT;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );
    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_ClockEdge( PSSI_CLOCK_EDGE_RISING ) );
    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_CKPOL, PSSI->CR & PSSI_CR_CKPOL );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_ClockEdge( &clockEdge ) );
    TEST_ASSERT_EQUAL( PSSI_CLOCK_EDGE_RISING, clockEdge );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_ClockEdge() rejects an out-of-range value.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_ClockEdge_InvalidValue_ReturnsError( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_ClockEdge( PSSI_CLOCK_EDGE_CNT ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Get_ClockEdge() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_ClockEdge_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_ClockEdge( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Set_Handshake() on an idle instance accepts PSSI_HANDSHAKE_DE_RDY with both
 *          polarities active high and sets PSSI_CR.DERDYCFG / DEPOL / RDYPOL accordingly;
 *          Pssi_Get_Handshake() reports the three values back (mutation check: DERDYCFG must be
 *          3, not the DE-only value 2 nor the RDY-only value 1 used by the Init test above).
 *
 * \par Expected results
 * - Both calls return PSSI_REQUEST_OK.
 * - PSSI_CR.DERDYCFG is 3, DEPOL and RDYPOL are set.
 * - The reported handshake / polarities match the request.
 */
void Ut_Pssi_Set_Handshake_Valid_SetsDerdyCfgAndPolarity( void )
{
#if defined(PSSI)
    pssi_Handshake_t handshake    = PSSI_HANDSHAKE_CNT;
    pssi_Polarity_t    dePolarity  = PSSI_POLARITY_CNT;
    pssi_Polarity_t    rdyPolarity = PSSI_POLARITY_CNT;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );
    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK,
                       Pssi_Set_Handshake( PSSI_HANDSHAKE_DE_RDY,
                                           PSSI_POLARITY_ACTIVE_HIGH,
                                           PSSI_POLARITY_ACTIVE_HIGH ) );

    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_DERDYCFG & UT_PSSI_CR_DERDYCFG_DE_RDY,
                              PSSI->CR & PSSI_CR_DERDYCFG );
    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_DEPOL, PSSI->CR & PSSI_CR_DEPOL );
    TEST_ASSERT_EQUAL_HEX32( PSSI_CR_RDYPOL, PSSI->CR & PSSI_CR_RDYPOL );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_Handshake( &handshake, &dePolarity, &rdyPolarity ) );
    TEST_ASSERT_EQUAL( PSSI_HANDSHAKE_DE_RDY, handshake );
    TEST_ASSERT_EQUAL( PSSI_POLARITY_ACTIVE_HIGH, dePolarity );
    TEST_ASSERT_EQUAL( PSSI_POLARITY_ACTIVE_HIGH, rdyPolarity );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Set_Handshake() rejects an out-of-range handshake value.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_Handshake_InvalidValue_ReturnsError( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR,
                       Pssi_Set_Handshake( PSSI_HANDSHAKE_CNT,
                                           PSSI_POLARITY_ACTIVE_LOW,
                                           PSSI_POLARITY_ACTIVE_LOW ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Get_Handshake() rejects a null pointer (any of the three).
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_Handshake_NullPtr_ReturnsError( void )
{
    pssi_Polarity_t dePolarity = PSSI_POLARITY_ACTIVE_LOW;
    pssi_Polarity_t rdyPolarity = PSSI_POLARITY_ACTIVE_LOW;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_Handshake( PSSI_NULL_PTR, &dePolarity, &rdyPolarity ) );
}

/* ================================ INTERRUPTS ===================================== */

/**
 * \brief   Pssi_Set_IrqPriority() rejects the request on an instance that was never initialized.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Set_IrqPriority_NotInitialized_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_IrqPriority( UT_PSSI_IRQ_PRIO ) );
}


/**
 * \brief   Pssi_Set_IrqPriority() on an initialized instance forwards the priority to Nvic.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_Set_IrqPriority_Initialized_SetsPriority( void )
{
#if defined(PSSI)
    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_DCMI_PSSI, UT_PSSI_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_IrqPriority( UT_PSSI_IRQ_PRIO ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_Get_IrqPriority() rejects a null pointer.
 *
 * \par Expected results
 * - PSSI_REQUEST_ERROR is returned.
 */
void Ut_Pssi_Get_IrqPriority_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_IrqPriority( PSSI_NULL_PTR ) );
}


/**
 * \brief   Pssi_Get_IrqPriority() on an initialized instance reads the priority back from Nvic.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_Get_IrqPriority_Initialized_ReturnsPriority( void )
{
#if defined(PSSI)
    pssi_IrqPrio_t irqPriority = 0u;

    UtPssi_Force_Initialized( PSSI_XFER_IDLE );

    Nvic_Get_PeriphIrq_Prio_ExpectAnyArgsAndReturn( NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_IrqPriority( &irqPriority ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}

/* ==================================== PINS ======================================= */

/**
 * \brief   Pssi_InitClockGpio() with the "unused" pin item does nothing and succeeds (no Gpio
 *          call, no mock expectation needed).
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitClockGpio_Unused_ReturnsOk( void )
{
#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitClockGpio( PSSI_CLOCK_PIN_UNUSED ) );
#else
    /* No PSSI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitClockGpio( PSSI_CLOCK_PIN_UNUSED ) );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitClockGpio() with a real pin item configures it through Gpio_Init.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitClockGpio_Used_ConfiguresGpio( void )
{
#if defined(PSSI)
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitClockGpio( PSSI_CLOCK_PIN_PA6 ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitDataGpio() with the "unused" pin item does nothing and succeeds.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitDataGpio_Unused_ReturnsOk( void )
{
#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitDataGpio( PSSI_DATA_PIN_UNUSED ) );
#else
    /* No PSSI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitDataGpio( PSSI_DATA_PIN_UNUSED ) );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitDataGpio() with a real pin item configures it through Gpio_Init.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitDataGpio_Used_ConfiguresGpio( void )
{
#if defined(PSSI)
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitDataGpio( PSSI_DATA_PIN_D0_PA9 ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitDeGpio() with the "unused" pin item does nothing and succeeds.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitDeGpio_Unused_ReturnsOk( void )
{
#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitDeGpio( PSSI_DE_PIN_UNUSED ) );
#else
    /* No PSSI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitDeGpio( PSSI_DE_PIN_UNUSED ) );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitDeGpio() with a real pin item configures it through Gpio_Init.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitDeGpio_Used_ConfiguresGpio( void )
{
#if defined(PSSI)
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitDeGpio( PSSI_DE_PIN_PA4 ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitRdyGpio() with the "unused" pin item does nothing and succeeds.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitRdyGpio_Unused_ReturnsOk( void )
{
#if defined(PSSI)
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitRdyGpio( PSSI_RDY_PIN_UNUSED ) );
#else
    /* No PSSI peripheral on this line: the peripheral check always rejects it */
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitRdyGpio( PSSI_RDY_PIN_UNUSED ) );
#endif /* PSSI */
}


/**
 * \brief   Pssi_InitRdyGpio() with a real pin item configures it through Gpio_Init.
 *
 * \par Expected results
 * - PSSI_REQUEST_OK is returned.
 */
void Ut_Pssi_InitRdyGpio_Used_ConfiguresGpio( void )
{
#if defined(PSSI)
    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_InitRdyGpio( PSSI_RDY_PIN_PB7 ) );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line" );
#endif /* PSSI */
}

/* ===================== LINES WITHOUT THE PSSI PERIPHERAL ====================== */

/**
 * \brief   Peripheral check of every public function on a line without the PSSI peripheral
 *          (STM32H503, STM32H543, STM32H553).
 *
 * \par Expected results
 * - Every function returns PSSI_REQUEST_ERROR and leaves its output parameters unchanged.
 * - Pssi_Task does nothing, Pssi_Get_ModuleVersion still returns the version of the module.
 */
void Ut_Pssi_NoPeripheral_AllFunctions_ReturnError( void )
{
#if !defined(PSSI)
    pssi_Config_t        config         = { PSSI_DATA_WIDTH_8 };
    pssi_Data_t          buffer[ UT_PSSI_BUFFER_SIZE_BYTE ];
    pssi_XferState_t     xferState      = PSSI_XFER_ERROR;
    pssi_DataCnt_t       transferredCnt = UT_PSSI_UNTOUCHED_VALUE;
    pssi_XferError_t     errorMask      = UT_PSSI_UNTOUCHED_VALUE;
    pssi_DataWidth_t     dataWidth      = PSSI_DATA_WIDTH_8;
    pssi_ClockEdge_t     clockEdge      = PSSI_CLOCK_EDGE_RISING;
    pssi_Handshake_t     handshake      = PSSI_HANDSHAKE_DE_RDY;
    pssi_Polarity_t      dePolarity     = PSSI_POLARITY_ACTIVE_HIGH;
    pssi_Polarity_t      rdyPolarity    = PSSI_POLARITY_ACTIVE_HIGH;
    pssi_IrqPrio_t       irqPriority    = UT_PSSI_IRQ_PRIO;
    pssi_ModuleVersion_t version        = Pssi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_DATA_WIDTH_8, config.DataWidth );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Init( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Deinit() );
    Pssi_Task();
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_RxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_TxStart( buffer, UT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_Abort() );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_ERROR, xferState );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferCount( &transferredCnt ) );
    TEST_ASSERT_EQUAL_UINT32( UT_PSSI_UNTOUCHED_VALUE, transferredCnt );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_XferError( &errorMask ) );
    TEST_ASSERT_EQUAL_UINT32( UT_PSSI_UNTOUCHED_VALUE, errorMask );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_DataWidth( PSSI_DATA_WIDTH_8 ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_DataWidth( &dataWidth ) );
    TEST_ASSERT_EQUAL( PSSI_DATA_WIDTH_8, dataWidth );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_ClockEdge( PSSI_CLOCK_EDGE_FALLING ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_ClockEdge( &clockEdge ) );
    TEST_ASSERT_EQUAL( PSSI_CLOCK_EDGE_RISING, clockEdge );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR,
                       Pssi_Set_Handshake( PSSI_HANDSHAKE_NONE, PSSI_POLARITY_ACTIVE_LOW, PSSI_POLARITY_ACTIVE_LOW ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_Handshake( &handshake, &dePolarity, &rdyPolarity ) );
    TEST_ASSERT_EQUAL( PSSI_HANDSHAKE_DE_RDY, handshake );
    TEST_ASSERT_EQUAL( PSSI_POLARITY_ACTIVE_HIGH, dePolarity );
    TEST_ASSERT_EQUAL( PSSI_POLARITY_ACTIVE_HIGH, rdyPolarity );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Set_IrqPriority( UT_PSSI_IRQ_PRIO ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_Get_IrqPriority( &irqPriority ) );
    TEST_ASSERT_EQUAL_UINT32( UT_PSSI_IRQ_PRIO, irqPriority );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitClockGpio( PSSI_CLOCK_PIN_UNUSED ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitDataGpio( PSSI_DATA_PIN_UNUSED ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitDeGpio( PSSI_DE_PIN_UNUSED ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_ERROR, Pssi_InitRdyGpio( PSSI_RDY_PIN_UNUSED ) );
#else
    TEST_IGNORE_MESSAGE( "MCU with PSSI" );
#endif /* PSSI */
}

/* ============================== LOCAL (Ut) HELPERS =============================== */

#if defined(PSSI)
/**
 * \brief Builds a minimal, valid configuration (no handshake, so no DE / RDY pin, no data pin;
 *        only the clock pin and the DMA / IRQ identification are set).
 *
 * \return Minimal valid configuration.
 */
static pssi_Config_t UtPssi_Get_MinimalConfig( void )
{
    pssi_Config_t config;

    (void)Pssi_Get_DefaultConfig( &config );
    config.ClockPin    = PSSI_CLOCK_PIN_PA6;
    config.IrqPriority = UT_PSSI_IRQ_PRIO;

    return ( config );
}


/**
 * \brief Puts the single PSSI instance into the initialized state with \p xferState, without
 *        calling Pssi_Init (see the file header note on PssiPriv_Get_Instance).
 *
 * \param xferState [in]: Transfer state to force.
 */
static void UtPssi_Force_Initialized( pssi_XferState_t xferState )
{
    pssi_InstanceData_t * instance = PSSI_NULL_PTR;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, PssiPriv_Get_Instance( &instance ) );

    instance->Initialized   = PSSI_FUNCTION_ACTIVE;
    instance->XferState      = xferState;
    instance->LastError       = 0u;
    instance->XferByteCnt      = 0u;
    instance->DmaChannelInit  = PSSI_FUNCTION_INACTIVE;
    (void)Pssi_Get_DefaultConfig( &instance->Config );
    instance->Config.ClockPin    = PSSI_CLOCK_PIN_PA6;
    instance->Config.IrqPriority = UT_PSSI_IRQ_PRIO;
}
#endif /* PSSI */
