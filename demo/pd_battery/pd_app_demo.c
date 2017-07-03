/*
 * Copyright 2016 - 2017 NXP
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

#include "usb_pd.h"
#include "board.h"
#include "pd_app.h"
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "pd_power_interface.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

void pd_port_task(void *arg);
void PD_AppInit(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

uint32_t PD_DemoStartUpGetBatteryValue(pd_app_t *pdAppInstance)
{
    return 30; /* default start up battery is 5% */
}

void PD_DemoBatteryChange(pd_app_t *pdAppInstance)
{
    if (!pdAppInstance->contractValid)
    {
        return;
    }

    if (pdAppInstance->selfPowerRole == kPD_PowerRoleSource)
    {
        if (pdAppInstance->batteryQuantity > 5)
        {
            pdAppInstance->batteryQuantity--;
        }

        /* battery consume faster */
        if (pdAppInstance->partnerRequestRDO.bitFields.objectPosition == 2)
        {
            if (pdAppInstance->batteryQuantity > 5)
            {
                pdAppInstance->batteryQuantity--;
            }
        }
    }
    else
    {
        if (pdAppInstance->batteryQuantity < 100)
        {
            pdAppInstance->batteryQuantity++;
        }

        /* battery charge faster */
        if (pdAppInstance->sinkRequestVoltage == PD_DEMO_BATTERY_CHARGE_REQUEST_VOLTAGE)
        {
            if (pdAppInstance->batteryQuantity < 100)
            {
                pdAppInstance->batteryQuantity++;
            }
        }
    }
}

void PD_DemoReset(pd_app_t *pdAppInstance)
{
    g_PowerPortConfig.sourceCaps = (uint32_t *)&pdAppInstance->selfSourcePdo1;
    if (pdAppInstance->batteryQuantity > 30)
    {
        g_PowerPortConfig.sourceCapCount = 2;
    }
    else
    {
        g_PowerPortConfig.sourceCapCount = 1;
    }
    pdAppInstance->partnerRequestRDO.bitFields.objectPosition = 0; /* invalid */
    pdAppInstance->contractValid = 0;
    pdAppInstance->demoState = kDemoState_Start;
    pdAppInstance->partnerSourceCapNumber = 0;
}

void PD_DemoInit(pd_app_t *pdAppInstance)
{
    pdAppInstance->batteryQuantity = PD_DemoStartUpGetBatteryValue(pdAppInstance);
    pdAppInstance->batteryChange = 0;
    PD_DemoReset(pdAppInstance);
    return;
}

void PD_Demo1msIsrProcess(pd_app_t *pdAppInstance)
{
    static volatile uint32_t delay = 0;
    delay++;

    /* 1.2 s */
    if (delay >= 1200)
    {
        delay = 0;
        PD_DemoBatteryChange(pdAppInstance);
        pdAppInstance->batteryChange = 1;
    }

    if (pdAppInstance->retrySwapDelay > 0)
    {
        pdAppInstance->retrySwapDelay--;
    }
}

pd_status_t PD_DemoFindPDO(pd_app_t *pdAppInstance, pd_rdo_t *rdo, uint32_t *voltage)
{
    uint32_t index;
    pd_source_pdo_t sourcePDO;
    uint32_t requestVoltage;
    uint32_t requestCurrent;
    uint8_t findSourceCap = 0;

    if (pdAppInstance->partnerSourceCapNumber == 0)
    {
        return kStatus_PD_Error;
    }

    /* default rdo as 5V - 0.9A */
    *voltage = 5000;
    rdo->bitFields.objectPosition = 1;
    rdo->bitFields.giveBack = 0;
    rdo->bitFields.capabilityMismatch = 0;
    rdo->bitFields.usbCommunicationsCapable = 0;
    rdo->bitFields.noUsbSuspend = 1;
    rdo->bitFields.operateValue = 500 / PD_PDO_CURRENT_UNIT;
    rdo->bitFields.maxOrMinOperateValue = rdo->bitFields.operateValue;
    if (pdAppInstance->batteryQuantity < 100)
    {
        requestVoltage = PD_DEMO_BATTERY_CHARGE_REQUEST_VOLTAGE;
        requestCurrent = PD_DEMO_BATTERY_CHARGE_REQUEST_CURRENT;
    }
    else
    {
        requestVoltage = PD_DEMO_BATTERY_FULL_REQUEST_VOLTAGE;
        requestCurrent = PD_DEMO_BATTERY_FULL_REQUEST_CURRENT;
    }

    for (index = 0; index < pdAppInstance->partnerSourceCapNumber; ++index)
    {
        sourcePDO.PDOValue = pdAppInstance->partnerSourceCaps[index].PDOValue;
        switch (sourcePDO.commonPDO.pdoType)
        {
            case kPDO_Fixed:
            {
                if ((sourcePDO.fixedPDO.voltage * PD_PDO_VOLTAGE_UNIT == requestVoltage) &&
                    (sourcePDO.fixedPDO.maxCurrent * PD_PDO_CURRENT_UNIT >= requestCurrent))
                {
                    *voltage = sourcePDO.fixedPDO.voltage * PD_PDO_VOLTAGE_UNIT;
                    rdo->bitFields.objectPosition = (index + 1);
                    rdo->bitFields.operateValue = requestCurrent / PD_PDO_CURRENT_UNIT;
                    rdo->bitFields.maxOrMinOperateValue = rdo->bitFields.operateValue;
                    findSourceCap = 1;
                }
                break;
            }

            case kPDO_Variable:
            {
                if ((sourcePDO.variablePDO.minVoltage * PD_PDO_VOLTAGE_UNIT <= requestVoltage) &&
                    (sourcePDO.variablePDO.maxVoltage * PD_PDO_VOLTAGE_UNIT >= requestVoltage) &&
                    (sourcePDO.variablePDO.maxCurrent * PD_PDO_CURRENT_UNIT >= requestCurrent))
                {
                    *voltage = sourcePDO.variablePDO.minVoltage * PD_PDO_VOLTAGE_UNIT;
                    rdo->bitFields.objectPosition = (index + 1);
                    rdo->bitFields.operateValue = requestCurrent / PD_PDO_CURRENT_UNIT;
                    rdo->bitFields.maxOrMinOperateValue = rdo->bitFields.operateValue;
                    findSourceCap = 1;
                }
                break;
            }

            case kPDO_Battery:
            {
                if ((sourcePDO.batteryPDO.minVoltage * PD_PDO_VOLTAGE_UNIT <= requestVoltage) &&
                    (sourcePDO.batteryPDO.maxVoltage * PD_PDO_VOLTAGE_UNIT >= requestVoltage) &&
                    (sourcePDO.batteryPDO.maxAllowPower * PD_PDO_POWER_UNIT >=
                     (requestVoltage * requestCurrent / 1000)))
                {
                    *voltage = sourcePDO.batteryPDO.minVoltage * PD_PDO_VOLTAGE_UNIT;
                    rdo->bitFields.objectPosition = (index + 1);
                    rdo->bitFields.operateValue = (requestVoltage * requestCurrent) / 1000 / PD_PDO_POWER_UNIT;
                    rdo->bitFields.maxOrMinOperateValue = rdo->bitFields.operateValue;
                    findSourceCap = 1;
                }
                break;
            }

            default:
                break;
        }

        if (findSourceCap)
        {
            break;
        }
    }

    return kStatus_PD_Success;
}

void PD_DemoPDReset(void)
{
    pd_handle pdTmpHandle;
    PD_PowerBoardReset();
    NVIC_DisableIRQ(PORTB_PORTC_PORTD_PORTE_IRQn);
    g_PDAppInstance.partnerSourceCapNumber = 0;
    g_PDAppInstance.contractValid = 0;
    g_PDAppInstance.demoState = kDemoState_Start;
    pdTmpHandle = g_PDAppInstance.pdHandle;
    g_PDAppInstance.pdHandle = NULL;
    vTaskDelete(g_PDAppInstance.pdTaskHandler);
    PD_InstanceDeinit(pdTmpHandle);
    PD_AppInit();
    if (xTaskCreate(pd_port_task, "port", (1024u + 512u) / sizeof(portSTACK_TYPE), NULL, 5,
                    &g_PDAppInstance.pdTaskHandler) != pdPASS)
    {
        PRINTF("create pd task error\r\n");
    }
    NVIC_EnableIRQ(PORTB_PORTC_PORTD_PORTE_IRQn);
}

void PD_DemoTaskFun(pd_app_t *pdAppInstance)
{
    pd_status_t status = kStatus_PD_Error;
    static uint8_t runningPowerRole = kPD_PowerRoleNone;
    static uint8_t trySwap = 0;
    static uint8_t retryCount = 0;

    if (!pdAppInstance->contractValid)
    {
        pdAppInstance->demoState = kDemoState_Start;
        return;
    }

    if (pdAppInstance->batteryChange == 1)
    {
        pdAppInstance->batteryChange = 0;

        uint8_t chargeVoltage;
        if (pdAppInstance->selfPowerRole == kPD_PowerRoleSource)
        {
            chargeVoltage = (pdAppInstance->partnerRequestRDO.bitFields.objectPosition == 2 ? 9 : 5);
            if (pdAppInstance->batteryQuantity <= 5)
            {
                chargeVoltage = 0;
            }
        }
        else
        {
            chargeVoltage = ((pdAppInstance->sinkRequestVoltage == PD_DEMO_BATTERY_FULL_REQUEST_VOLTAGE) ? 5 : 9);
        }
        PRINTF("battery percent:%d, charge voltage:%dV\r\n", pdAppInstance->batteryQuantity, chargeVoltage);
    }

    if (runningPowerRole != pdAppInstance->selfPowerRole)
    {
        pdAppInstance->demoState = kDemoState_Start;
        runningPowerRole = pdAppInstance->selfPowerRole;
        if (runningPowerRole == kPD_PowerRoleSource)
        {
            pdAppInstance->prSwapAccept = 1u;
        }
        else
        {
            pdAppInstance->prSwapAccept = 0u;
        }
    }

    if (pdAppInstance->demoState == kDemoState_Start)
    {
        if (runningPowerRole == kPD_PowerRoleSource)
        {
            pdAppInstance->demoState = kDemoState_TryChangeAsSink;
        }
        else
        {
            pdAppInstance->demoState = kDemoState_Idle;
        }
    }

    if (pdAppInstance->batteryQuantity > 30)
    {
        if (g_PowerPortConfig.sourceCapCount == 1)
        {
            g_PowerPortConfig.sourceCaps = (uint32_t *)&pdAppInstance->selfSourcePdo1;
            g_PowerPortConfig.sourceCapCount = 2;
            PRINTF("change source caps as high power(5V/9V)\r\n");
        }
    }
    else
    {
        if (g_PowerPortConfig.sourceCapCount == 2)
        {
            g_PowerPortConfig.sourceCaps = (uint32_t *)&pdAppInstance->selfSourcePdo1;
            g_PowerPortConfig.sourceCapCount = 1;
            PRINTF("change source caps as low power(5V)\r\n");
        }
    }

    /* reference to readme */
    if (runningPowerRole == kPD_PowerRoleSource)
    {
        pdAppInstance->prSwapAccept = 1;

        switch (pdAppInstance->demoState)
        {
            case kDemoState_Idle:
                if (pdAppInstance->batteryQuantity > 5)
                {
                    g_PowerPortConfig.drpTryFunction = kTypecTry_None;
                }

                if ((pdAppInstance->batteryQuantity < 30) &&
                    (pdAppInstance->partnerRequestRDO.bitFields.objectPosition == 2))
                {
                    g_PowerPortConfig.sourceCaps = (uint32_t *)&pdAppInstance->selfSourcePdo1;
                    g_PowerPortConfig.sourceCapCount = 1;
                    pdAppInstance->partnerRequestRDO.bitFields.objectPosition = 0; /* invalid */
                    status = PD_Command(pdAppInstance->pdHandle, PD_DPM_CONTROL_POWER_NEGOTIATION, NULL);
                    if (status != kStatus_PD_Success)
                    {
                        PRINTF("error\r\n");
                    }
                }
                else if (pdAppInstance->batteryQuantity <= 20)
                {
                    pdAppInstance->demoState = kDemoState_NeedChangeAsSink;
                }
                else
                {
                }
                break;

            case kDemoState_TryChangeAsSink:
                PRINTF("try to swap as sink\r\n");
                trySwap = 1;
                retryCount = 2;
                pdAppInstance->demoState = kDemoState_GetPartnerSrcCap;
                break;

            case kDemoState_NeedChangeAsSink:
                PRINTF("need change as sink because battery is low\r\n");
                trySwap = 0;
                retryCount = 2;
                if (pdAppInstance->partnerSourceCapNumber > 0)
                {
                    retryCount = 2;
                    pdAppInstance->demoState = kDemoState_SwapAsSink;
                }
                else
                {
                    pdAppInstance->demoState = kDemoState_GetPartnerSrcCap;
                }
                break;

            case kDemoState_GetPartnerSrcCap:
                if (retryCount > 0)
                {
                    PRINTF("get partner source caps\r\n");
                    /* get partner source cap */
                    pdAppInstance->commandWait = 1;
                    pdAppInstance->commandResult = kCommandResult_Error;
                    status = PD_Command(pdAppInstance->pdHandle, PD_DPM_CONTROL_GET_PARTNER_SOURCE_CAPABILITIES, NULL);
                    if (status == kStatus_PD_Success)
                    {
                        pdAppInstance->demoState = kDemoState_WaitPartnerSrcCap;
                    }
                    else
                    {
                        PRINTF("error\r\n");
                    }
                    retryCount--;
                }
                else
                {
                    PRINTF("retry fail\r\n");
                    if (trySwap)
                    {
                        pdAppInstance->demoState = kDemoState_Idle;
                    }
                    else
                    {
                        pdAppInstance->demoState = kDemoState_SrcReducePower;
                    }
                }
                break;

            case kDemoState_WaitPartnerSrcCap:
                if (pdAppInstance->commandWait == 0)
                {
                    if (pdAppInstance->commandResult == kCommandResult_Success)
                    {
                        pd_rdo_t rdo;
                        uint32_t voltage;
                        PRINTF("receive source capabilities\r\n");

                        if (PD_DemoFindPDO(pdAppInstance, &rdo, &voltage) == kStatus_PD_Success)
                        {
                            if (trySwap)
                            {
                                if (pdAppInstance->partnerSourceCaps[0].fixedPDO.externalPowered)
                                {
                                    retryCount = 2;
                                    pdAppInstance->demoState = kDemoState_SwapAsSink;
                                }
                                else
                                {
                                    pdAppInstance->demoState = kDemoState_Idle;
                                }
                            }
                            else
                            {
                                retryCount = 2;
                                pdAppInstance->demoState = kDemoState_SwapAsSink;
                            }
                        }
                        else
                        {
                            PRINTF("source cap cannot satisfy reqeust\r\n");
                            if (trySwap)
                            {
                                pdAppInstance->demoState = kDemoState_Idle;
                            }
                            else
                            {
                                pdAppInstance->demoState = kDemoState_SrcReducePower;
                            }
                        }
                    }
                    else if (pdAppInstance->commandResult == kCommandResult_NotSupported)
                    {
                        PRINTF("partner don't support src cap\r\n");
                        if (trySwap)
                        {
                            pdAppInstance->demoState = kDemoState_Idle;
                        }
                        else
                        {
                            pdAppInstance->demoState = kDemoState_SrcReducePower;
                        }
                        PRINTF("partner don't support source function\r\n");
                    }
                    else
                    {
                        pdAppInstance->demoState = kDemoState_GetPartnerSrcCap;
                    }
                }
                break;

            case kDemoState_SwapAsSink:
                if (retryCount > 0)
                {
                    PRINTF("try pr swap\r\n");
                    pdAppInstance->commandWait = 1;
                    pdAppInstance->commandResult = kCommandResult_Error;
                    status = PD_Command(pdAppInstance->pdHandle, PD_DPM_CONTROL_PR_SWAP, NULL);
                    if (status == kStatus_PD_Success)
                    {
                        pdAppInstance->demoState = kDemoState_WaitPRSwap;
                    }
                    else
                    {
                        PRINTF("error\r\n");
                    }
                    retryCount--;
                }
                else
                {
                    PRINTF("retry fail\r\n");
                    if (trySwap)
                    {
                        pdAppInstance->demoState = kDemoState_Idle;
                    }
                    else
                    {
                        pdAppInstance->demoState = kDemoState_SrcReducePower;
                    }
                }
                break;

            case kDemoState_WaitPRSwap:
                if (pdAppInstance->commandWait == 0)
                {
                    if (pdAppInstance->commandResult == kCommandResult_Success)
                    {
                        /* code cannot excute here, will go to the sink code */
                    }
                    else if ((pdAppInstance->commandResult == kCommandResult_NotSupported) ||
                             (pdAppInstance->commandResult == kCommandResult_Reject))
                    {
                        PRINTF("swap reject\r\n");
                        if (trySwap)
                        {
                            pdAppInstance->demoState = kDemoState_Idle;
                        }
                        else
                        {
                            pdAppInstance->demoState = kDemoState_SrcReducePower;
                        }
                    }
                    else
                    {
                        pdAppInstance->demoState = kDemoState_SwapAsSink; /* retry */
                    }
                }
                break;

            case kDemoState_SrcReducePower:
                if (pdAppInstance->partnerRequestRDO.bitFields.objectPosition != 1)
                {
                    PRINTF("source reduce power\r\n");
                    g_PowerPortConfig.sourceCaps = (uint32_t *)&pdAppInstance->selfSourcePdo1;
                    g_PowerPortConfig.sourceCapCount = 1;
                    PRINTF("change source caps as low power(5V)\r\n");
                    pdAppInstance->partnerRequestRDO.bitFields.objectPosition = 0; /* invalid */
                    status = PD_Command(pdAppInstance->pdHandle, PD_DPM_CONTROL_POWER_NEGOTIATION, NULL);
                    if (status != kStatus_PD_Success)
                    {
                        PRINTF("error\r\n");
                    }
                }

                pdAppInstance->retrySwapDelay = 5 * 1000; /* 5s */
                pdAppInstance->demoState = kDemoState_SwapSinkFail;
                break;

            case kDemoState_SwapSinkFail:
                if (pdAppInstance->batteryQuantity <= 5)
                {
                    PD_DemoPDReset();
                    g_PowerPortConfig.drpTryFunction = kTypecTry_Snk;
                    pdAppInstance->demoState = kDemoState_Start;
                }
                else
                {
                    if (pdAppInstance->retrySwapDelay == 0)
                    {
                        PRINTF("try swap again\r\n");
                        pdAppInstance->demoState = kDemoState_NeedChangeAsSink;
                    }
                }
                break;

            case kDemoState_NoPower:
                PD_DemoPDReset();
                break;

            default:
                break;
        }
    }
    else
    {
        if (pdAppInstance->batteryQuantity > 30)
        {
            pdAppInstance->prSwapAccept = 1u;
        }
        else
        {
            pdAppInstance->prSwapAccept = 0u;
        }

        switch (pdAppInstance->demoState)
        {
            case kDemoState_Idle:
                if (pdAppInstance->batteryQuantity >= 100)
                {
                    /* rquest low power */
                    if (pdAppInstance->sinkRequestVoltage == PD_DEMO_BATTERY_CHARGE_REQUEST_VOLTAGE)
                    {
                        retryCount = 2;
                        pdAppInstance->demoState = kDemoState_RequestLowPower;
                    }
                }
                break;

            case kDemoState_RequestLowPower:
                if (retryCount > 0)
                {
                    /* default rdo as 5V - 0.9A */
                    pdAppInstance->sinkRequestVoltage = 5000;
                    pdAppInstance->sinkRequestRDO.bitFields.objectPosition = 1;
                    pdAppInstance->sinkRequestRDO.bitFields.giveBack = 0;
                    pdAppInstance->sinkRequestRDO.bitFields.capabilityMismatch = 0;
                    pdAppInstance->sinkRequestRDO.bitFields.usbCommunicationsCapable = 0;
                    pdAppInstance->sinkRequestRDO.bitFields.noUsbSuspend = 1;
                    pdAppInstance->sinkRequestRDO.bitFields.operateValue = 500 / PD_PDO_CURRENT_UNIT;
                    pdAppInstance->sinkRequestRDO.bitFields.maxOrMinOperateValue =
                        pdAppInstance->sinkRequestRDO.bitFields.operateValue;
                    /* get partner source cap */
                    pdAppInstance->commandWait = 1;
                    pdAppInstance->commandResult = kCommandResult_Error;
                    status =
                        PD_Command(pdAppInstance->pdHandle, PD_DPM_CONTROL_REQUEST, &pdAppInstance->sinkRequestRDO);
                    if (status == kStatus_PD_Success)
                    {
                        pdAppInstance->demoState = kDemoState_WaitRequestLowPower;
                    }
                    else
                    {
                        PRINTF("error\r\n");
                    }
                }
                else
                {
                    PRINTF("retry fail\r\n");
                    pdAppInstance->demoState = kDemoState_Idle;
                }
                break;

            case kDemoState_WaitRequestLowPower:
                if (pdAppInstance->commandWait == 0)
                {
                    if (pdAppInstance->commandResult == kCommandResult_Success)
                    {
                        pdAppInstance->demoState = kDemoState_Idle;
                    }
                    else
                    {
                        pdAppInstance->demoState = kDemoState_RequestLowPower; /* retry */
                    }
                }
                break;

            default:
                break;
        }
    }
    return;
}
