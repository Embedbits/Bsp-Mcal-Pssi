/**
 * \author Mr.Nobody
 * \file Pssi_Reg.c
 * \ingroup Pssi
 * \brief Pssi module Reg component functionality.
 *
 * Register access of the module: generic accessors (bounds checked register ID) and the
 * register protocol of the parallel synchronous slave interface. The registers are written
 * only here, the masks come from the CMSIS device header.
 *
 * Properties of the registers, which the protocol respects (RM0481 / RM0492 / RM0517, PSSI
 * chapter):
 * - PSSI_CR.ENABLE and PSSI and DCMI share the same circuitry: PSSI_CR.ENABLE and
 *   DCMI_CR.ENABLE must never both be 1 (see the module README "Shared pins").
 * - While PSSI_CR.ENABLE is 1, no other bit of PSSI_CR may be changed except ENABLE itself
 *   (RM0481 34.5.1 note); the module always clears ENABLE before changing OUTEN, EDM, CKPOL,
 *   DEPOL, RDYPOL or DERDYCFG and sets it again afterwards.
 * - PSSI_RIS is read only, PSSI_ICR is write only (setting a bit clears the matching PSSI_RIS
 *   bit, writing 0 has no effect).
 *
 */
/* ============================== INCLUDES ================================== */
#include "Pssi_Reg.h"                       /* Self include                   */
#include "Pssi_Types.h"                     /* Module types definitions       */
#include "Stm32.h"                          /* PSSI registers definition      */
/* ============================== TYPEDEFS ================================== */

#if defined(PSSI)

/** \brief Structure type used to store physical address of PSSI registers.
 * This array is used to reduce size of configuration array. With this style
 * the registers are referenced through enumeration.
 *
 */
typedef struct
{
    pssi_RegId_t        RegId;   /**< Peripheral register ID      */
    volatile uint32_t * RegAddr; /**< Peripheral register address */
}   pssi_RegList_t;

/* ======================== FORWARD DECLARATIONS ============================ */

static pssi_FunctionState_t Pssi_Reg_Get_BitState   ( pssi_RegId_t regId, uint32_t bitMask );

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** \brief Count of read-back iterations of a written register value */
#define PSSI_REG_READBACK_TIMEOUT_RAW   ( 0x1000u )

/** \brief Register value without any bit set (masked register value of an inactive bit field) */
#define PSSI_REG_BITS_CLEAR             ( 0u )

/* =============================== MACROS =================================== */

/* ========================== EXPORTED VARIABLES ============================ */

/* =========================== LOCAL VARIABLES ============================== */

/** \brief PSSI registers configuration structure
 *
 * This structure is used to store addresses of PSSI registers.
 * It is used to reduce code size while referring to PSSI registers.
 */
static const pssi_RegList_t             pssi_RegList[] =
{
    { .RegId = PSSI_REG_CR     , .RegAddr = &(PSSI->CR )  }, /**< PSSI control register        */
    { .RegId = PSSI_REG_RIS    , .RegAddr = &(PSSI->RIS)  }, /**< PSSI raw interrupt status reg */
    { .RegId = PSSI_REG_IER    , .RegAddr = &(PSSI->IER)  }, /**< PSSI interrupt enable register */
    { .RegId = PSSI_REG_ICR    , .RegAddr = &(PSSI->ICR)  }, /**< PSSI interrupt clear register */
};

_Static_assert( (sizeof(pssi_RegList) / sizeof(pssi_RegList_t)) == PSSI_REG_CNT,
                "Pssi_Reg: pssi_RegList has incorrect size." );

/* ========================= EXPORTED FUNCTIONS ============================= */

/**
 * \brief Sets the register bit with given mask.
 *
 * \param regId   [in]: PSSI register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Pssi_Set_RegBit( pssi_RegId_t regId, uint32_t bitMask )
{
    if( PSSI_REG_CNT > regId )
    {
        *pssi_RegList[regId].RegAddr |= bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Resets the register bit with given mask.
 *
 * \param regId   [in]: PSSI register ID
 * \param bitMask [in]: Mask to apply to the register
 */
void Pssi_Reset_RegBit( pssi_RegId_t regId, uint32_t bitMask )
{
    if( PSSI_REG_CNT > regId )
    {
        *pssi_RegList[regId].RegAddr &= ~bitMask;
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register bit value with given mask.
 *
 * \param regId   [in]: PSSI register ID
 * \param bitMask [in]: Mask to apply to the register
 * \return Register bit value with applied mask, no bit set for invalid register ID
 */
uint32_t Pssi_Get_RegBit( pssi_RegId_t regId, uint32_t bitMask )
{
    uint32_t retValue = PSSI_REG_BITS_CLEAR;

    if( PSSI_REG_CNT > regId )
    {
        retValue = ( *pssi_RegList[regId].RegAddr & bitMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = PSSI_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Updates the register value with given mask and value.
 *
 * \param regId    [in]: PSSI register ID
 * \param regMask  [in]: Mask to apply to the register
 * \param regValue [in]: Value to set in the register
 */
void Pssi_Set_RegVal( pssi_RegId_t regId, uint32_t regMask, uint32_t regValue )
{
    if( PSSI_REG_CNT > regId )
    {
        /* Set the register value with mask */
        *pssi_RegList[regId].RegAddr = ( ( *pssi_RegList[regId].RegAddr & ~regMask ) |
                                         ( regValue & regMask ) );
    }
    else
    {
        /* Invalid register ID, no action required */
    }
}


/**
 * \brief Gets the register value with given mask.
 *
 * \param regId   [in]: PSSI register ID.
 * \param regMask [in]: Mask to apply to the register
 * \return Register value with applied mask, no bit set for invalid register ID
 */
uint32_t Pssi_Get_RegVal( pssi_RegId_t regId, uint32_t regMask )
{
    uint32_t retValue = PSSI_REG_BITS_CLEAR;

    if( PSSI_REG_CNT > regId )
    {
        /* Get the register value with mask */
        retValue = ( *pssi_RegList[regId].RegAddr & regMask );
    }
    else
    {
        /* Invalid register ID, no bit is set */
        retValue = PSSI_REG_BITS_CLEAR;
    }

    return ( retValue );
}


/**
 * \brief Returns the address of the PSSI data register (PSSI_DR), used as the Gpdma source or
 *        destination address of a transfer. The register itself is never read or written by
 *        software.
 *
 * \return Address of PSSI_DR.
 */
uint32_t Pssi_Reg_Get_DataRegAddr( void )
{
    return ( (uint32_t)&(PSSI->DR) );
}


/**
 * \brief Sets PSSI_CR.ENABLE and verifies it by read-back.
 *
 * \return Returns \ref PSSI_REQUEST_OK if the bit read back within the timeout. Otherwise
 *         returns \ref PSSI_REQUEST_ERROR.
 */
pssi_RequestState_t Pssi_Reg_Set_Enable( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    Pssi_Set_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    for( uint32_t iterationCnt = 0u; PSSI_REG_READBACK_TIMEOUT_RAW > iterationCnt; iterationCnt++ )
    {
        const pssi_FunctionState_t enableState = Pssi_Reg_Get_EnableState();

        if( PSSI_FUNCTION_ACTIVE == enableState )
        {
            retState = PSSI_REQUEST_OK;
            break;
        }
        else
        {
            /* Not confirmed yet */
            retState = PSSI_REQUEST_ERROR;
        }
    }

    return ( retState );
}


/**
 * \brief Resets PSSI_CR.ENABLE and verifies it by read-back.
 *
 * \return Returns \ref PSSI_REQUEST_OK if the bit read back within the timeout. Otherwise
 *         returns \ref PSSI_REQUEST_ERROR.
 */
pssi_RequestState_t Pssi_Reg_Set_Disable( void )
{
    pssi_RequestState_t retState = PSSI_REQUEST_ERROR;

    Pssi_Reset_RegBit( PSSI_REG_CR, PSSI_CR_ENABLE );

    for( uint32_t iterationCnt = 0u; PSSI_REG_READBACK_TIMEOUT_RAW > iterationCnt; iterationCnt++ )
    {
        const pssi_FunctionState_t enableState = Pssi_Reg_Get_EnableState();

        if( PSSI_FUNCTION_INACTIVE == enableState )
        {
            retState = PSSI_REQUEST_OK;
            break;
        }
        else
        {
            /* Not confirmed yet */
            retState = PSSI_REQUEST_ERROR;
        }
    }

    return ( retState );
}


/**
 * \brief Returns the PSSI_CR.ENABLE bit.
 *
 * \return \ref PSSI_FUNCTION_ACTIVE if the interface is enabled, otherwise
 *         \ref PSSI_FUNCTION_INACTIVE.
 */
pssi_FunctionState_t Pssi_Reg_Get_EnableState( void )
{
    return ( Pssi_Reg_Get_BitState( PSSI_REG_CR, PSSI_CR_ENABLE ) );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Returns the state of a register bit field as module enumeration.
 *
 * \param regId   [in]: PSSI register ID
 * \param bitMask [in]: Mask of the checked bits
 *
 * \return \ref PSSI_FUNCTION_ACTIVE if any bit of the mask is set, otherwise
 *         \ref PSSI_FUNCTION_INACTIVE (also for an invalid register ID).
 */
static pssi_FunctionState_t Pssi_Reg_Get_BitState( pssi_RegId_t regId, uint32_t bitMask )
{
    pssi_FunctionState_t retState = PSSI_FUNCTION_INACTIVE;
    const uint32_t         bitValue = Pssi_Get_RegBit( regId, bitMask );

    if( PSSI_REG_BITS_CLEAR != bitValue )
    {
        retState = PSSI_FUNCTION_ACTIVE;
    }
    else
    {
        retState = PSSI_FUNCTION_INACTIVE;
    }

    return ( retState );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */

#endif /* PSSI */
