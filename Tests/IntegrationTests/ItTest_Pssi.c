/**
 * \author Mr.Nobody
 * \file ItTest_Pssi.c
 * \ingroup Pssi
 * \brief Integration tests of the Pssi module on target.
 *
 * Pssi module runs on the MCU together with the real PSSI / GPIO / GPDMA / RCC / NVIC / CACHE
 * hardware. A bare Nucleo board carries no PSSI master device, so these tests verify what a
 * single board without an external master can prove without one: the module initializes and
 * deinitializes without a fault (no DE/RDY handshake, so only the PSSI_PDCK pin is needed - no
 * data/DE/RDY pin wiring is required by the configuration itself), the handshake / data width /
 * clock edge setters accept a configuration without a fault, and arming a receive or transmit
 * transfer with no external clock ever toggling (no master connected) leaves the module in the
 * running state instead of faulting or completing.
 *
 * Used resources:
 * - PSSI (shared RCC_PERIPH_DCMI_PSSI, see the module README "Shared pins" note - Dcmi is not
 *   touched by this test set)
 * - GPIO PA6 (PSSI_PDCK, present on every STM32H5 line with PSSI) - not wired to a real master,
 *   only the configuration and the running state are checked (no fault)
 * - GPDMA (one channel, GPDMA_PERIPH_1 / GPDMA_CHANNEL_0, never completes without a clock)
 *
 * \note On STM32H503 / STM32H543 / STM32H553 (no PSSI peripheral) every test is ignored
 *       (`TEST_IGNORE_MESSAGE`), see the module README availability table.
 * \note The tests run on the boards of the IT agent that have a PSSI (STM32H533, STM32H563).
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "IntegrationTesting.h"             /* Integration testing on target  */
#include "Pssi_Port.h"                      /* Module under test              */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

static pssi_RequestState_t ItPssi_Get_MinimalConfig( pssi_Config_t * const config );

/* ========================= SYMBOLIC CONSTANTS ============================= */

/** Interrupt priority used by the tests */
#define IT_PSSI_IRQ_PRIO                      ( 5u )

/** Transfer buffer size used by the start tests (a multiple of 4, arbitrary) */
#define IT_PSSI_BUFFER_SIZE_BYTE              ( 64u )

/* ========================== LOCAL VARIABLES =============================== */

/** Receive buffer used by the receive start test (DMA reachable, static - not on the test stack) */
static pssi_Data_t itPssi_RxBuffer[ IT_PSSI_BUFFER_SIZE_BYTE ];

/** Transmit buffer used by the transmit start test (DMA reachable, static, content irrelevant -
 *  no external master ever samples the bus in this test) */
static pssi_Data_t itPssi_TxBuffer[ IT_PSSI_BUFFER_SIZE_BYTE ];

/* ============================= TEST SETUP ================================= */

void setUp( void )
{
    /* No per-test state */
}


void tearDown( void )
{
    /* Peripheral state is cleared by system reset after the test case */
}

/* ========================== LOCAL FUNCTIONS ================================ */

/**
 * \brief Fills a default configuration with no handshake and the common Nucleo PSSI_PDCK pin
 *        (PA6, shared with DCMI_PIXCLK) - no DE/RDY/data pin is required by this mode.
 *
 * \param config [out]: Pointer to the configuration structure filled in place.
 *
 * \return PSSI_REQUEST_OK on success, PSSI_REQUEST_ERROR otherwise.
 */
static pssi_RequestState_t ItPssi_Get_MinimalConfig( pssi_Config_t * const config )
{
    pssi_RequestState_t retState = Pssi_Get_DefaultConfig( config );

#if defined(PSSI)
    if( PSSI_REQUEST_OK == retState )
    {
        config->IrqPriority = IT_PSSI_IRQ_PRIO;
        config->ClockPin    = PSSI_CLOCK_PIN_PA6;
    }
    else
    {
        /* Default configuration itself failed, nothing else to fill */
    }
#endif /* PSSI */

    return ( retState );
}

/* =============================== TESTS ==================================== */

/**
 * \brief The module version is reported without a fault.
 * \par Expected results
 * \ref Pssi_Get_ModuleVersion returns major version 1.
 */
void It_Pssi_Get_ModuleVersion_ReturnsVersion( void )
{
    pssi_ModuleVersion_t version = Pssi_Get_ModuleVersion();

    TEST_ASSERT_EQUAL_UINT8( 1u, version.Major );
}


/**
 * \brief The module initializes with no handshake and the common Nucleo PSSI_PDCK pin and
 *        deinitializes, without a fault (no external master connected, so the clock never
 *        toggles and no transfer is ever armed in this test).
 * \par Expected results
 * \ref Pssi_Init returns \ref PSSI_REQUEST_OK, \ref Pssi_Deinit returns \ref PSSI_REQUEST_OK.
 */
void It_Pssi_Init_MinimalConfig_ReturnsOk( void )
{
#if defined(PSSI)
    pssi_Config_t config;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, ItPssi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Deinit() );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line." );
#endif /* PSSI */
}


/**
 * \brief Arming a receive transfer with no external master connected (clock never toggles)
 *        leaves the module in the receiving state instead of faulting or completing; aborting
 *        and deinitializing afterwards does not fault either.
 * \par Expected results
 * \ref Pssi_Set_RxStart returns \ref PSSI_REQUEST_OK, the transfer state is
 * \ref PSSI_XFER_RECEIVING, \ref Pssi_Set_Abort and \ref Pssi_Deinit return
 * \ref PSSI_REQUEST_OK.
 */
void It_Pssi_Set_RxStart_NoExternalMaster_StaysReceivingWithoutFault( void )
{
#if defined(PSSI)
    pssi_Config_t      config;
    pssi_XferState_t     xferState = PSSI_XFER_ERROR;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, ItPssi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_RxStart( itPssi_RxBuffer, IT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_RECEIVING, xferState );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_Abort() );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Deinit() );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line." );
#endif /* PSSI */
}


/**
 * \brief Arming a transmit transfer with no external master connected (clock never toggles)
 *        leaves the module in the transmitting state instead of faulting or completing;
 *        aborting and deinitializing afterwards does not fault either.
 * \par Expected results
 * \ref Pssi_Set_TxStart returns \ref PSSI_REQUEST_OK, the transfer state is
 * \ref PSSI_XFER_TRANSMITTING, \ref Pssi_Set_Abort and \ref Pssi_Deinit return
 * \ref PSSI_REQUEST_OK.
 */
void It_Pssi_Set_TxStart_NoExternalMaster_StaysTransmittingWithoutFault( void )
{
#if defined(PSSI)
    pssi_Config_t      config;
    pssi_XferState_t     xferState = PSSI_XFER_ERROR;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, ItPssi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_TxStart( itPssi_TxBuffer, IT_PSSI_BUFFER_SIZE_BYTE ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Get_XferState( &xferState ) );
    TEST_ASSERT_EQUAL( PSSI_XFER_TRANSMITTING, xferState );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Set_Abort() );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Deinit() );
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line." );
#endif /* PSSI */
}


/**
 * \brief Setting the DE/RDY handshake while idle is accepted without a fault (the DE and RDY
 *        pins themselves are not wired on this board, only the register configuration path is
 *        exercised).
 * \par Expected results
 * \ref Pssi_Set_Handshake returns \ref PSSI_REQUEST_OK.
 */
void It_Pssi_Set_Handshake_DeRdy_ReturnsOkNoFault( void )
{
#if defined(PSSI)
    pssi_Config_t config;

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, ItPssi_Get_MinimalConfig( &config ) );
    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK, Pssi_Init( &config ) );

    TEST_ASSERT_EQUAL( PSSI_REQUEST_OK,
                       Pssi_Set_Handshake( PSSI_HANDSHAKE_DE_RDY,
                                           PSSI_POLARITY_ACTIVE_HIGH,
                                           PSSI_POLARITY_ACTIVE_HIGH ) );

    (void)Pssi_Deinit();
#else
    TEST_IGNORE_MESSAGE( "No PSSI peripheral on this line." );
#endif /* PSSI */
}
