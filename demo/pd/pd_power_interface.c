/*
 * Copyright 2016 - 2017 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "usb_pd_config.h"
#include "usb_pd.h"
#include "pd_power_interface.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

// request data object, 
static void PD_PowerGetVbusVoltage(uint32_t *partnerSourceCaps, pd_rdo_t rdo, pd_vbus_power_t *vbusPower)
{
    pd_source_pdo_t pdo;

    if (partnerSourceCaps == NULL)
    {
        return;
    }

    vbusPower->requestValue = rdo.bitFields.operateValue;
    pdo.PDOValue = partnerSourceCaps[rdo.bitFields.objectPosition - 1];
    switch (pdo.commonPDO.pdoType)
    {
        case kPDO_Fixed:
            vbusPower->minVoltage = pdo.fixedPDO.voltage;
            vbusPower->maxVoltage = pdo.fixedPDO.voltage;
            vbusPower->valueType = kRequestPower_Current; /* current */
            break;

        case kPDO_Battery:
            vbusPower->minVoltage = pdo.batteryPDO.minVoltage;
            vbusPower->maxVoltage = pdo.batteryPDO.maxVoltage;
            vbusPower->valueType = kRequestPower_Power; /* power */
            break;

        case kPDO_Variable:
            vbusPower->minVoltage = pdo.variablePDO.minVoltage;
            vbusPower->maxVoltage = pdo.variablePDO.maxVoltage;
            vbusPower->valueType = kRequestPower_Current; /* current */
            break;

        default:
            break;
    }
}

/*...............source need implement follow vbus power related functions..................*/

//Question: power部分的代码对应的硬件是那部分？
pd_status_t PD_PowerSrcTurnOnDefaultVbus(void *callbackParam, uint8_t powerProgress)
{
    pd_vbus_power_t vbusPower;

    vbusPower.valueType = kRequestPower_Current;
    vbusPower.minVoltage = vbusPower.maxVoltage = VSAFE5V_IN_50MV;
    vbusPower.requestValue = 0;
    PD_PowerBoardSourceEnableVbusPower(vbusPower);
    return kStatus_PD_Success;
}

pd_status_t PD_PowerSrcTurnOnRequestVbus(void *callbackParam, pd_rdo_t rdo)
{
    pd_vbus_power_t vbusPower;

    PD_PowerGetVbusVoltage(PD_PowerBoardGetSelfSourceCaps(callbackParam), rdo, &vbusPower);

    PD_PowerBoardSourceEnableVbusPower(vbusPower);
    return kStatus_PD_Success;
}

pd_status_t PD_PowerSrcTurnOffVbus(void *callbackParam, uint8_t powerProgress)
{
    PD_PowerBoardReset();
    return kStatus_PD_Success;
}

pd_status_t PD_PowerSrcGotoMinReducePower(void *callbackParam)
{
    /* in normal situation, only current is reduced,
       voltage don't change, so don't need any operation */
    return kStatus_PD_Success;
}

/*...............sink need implement follow vbus power related functions..................*/

pd_status_t PD_PowerSnkDrawTypeCVbus(void *callbackParam, uint8_t typecCurrentLevel, uint8_t powerProgress)
{
    pd_vbus_power_t vbusPower;

    vbusPower.valueType = kRequestPower_Current;
    vbusPower.minVoltage = vbusPower.maxVoltage = VSAFE5V_IN_50MV;
    vbusPower.requestValue = 0;
    PD_PowerBoardSinkEnableVbusPower(vbusPower);
    return kStatus_PD_Success;
}

pd_status_t PD_PowerSnkDrawRequestVbus(void *callbackParam, pd_rdo_t rdo)
{
    pd_vbus_power_t vbusPower;

    PD_PowerGetVbusVoltage(PD_PowerBoardGetPartnerSourceCaps(callbackParam), rdo, &vbusPower);
    if (vbusPower.minVoltage >= PD_POWER_REQUEST_MIN_VOLTAGE) /* 5V */
    {
        PD_PowerBoardSinkEnableVbusPower(vbusPower);
    }

    return kStatus_PD_Success;
}

pd_status_t PD_PowerSnkStopDrawVbus(void *callbackParam, uint8_t powerProgress)
{
    PD_PowerBoardReset();
    return kStatus_PD_Success;
}

pd_status_t PD_PowerSnkGotoMinReducePower(void *callbackParam)
{
    /* in normal situation, only current is reduced,
       voltage don't change, so don't need any operation */
    return kStatus_PD_Success;
}

/*................ if support vconn, need implement the follow related functions..........*/
pd_status_t PD_PowerControlVconn(void *callbackParam, uint8_t on)
{
    PD_PowerBoardControlVconn(on);
    return kStatus_PD_Success;
}
