/***********************************************************************************************************************
 * File Name    : hal_entry.c
 * Description  : Contains data structures and functions used in hal_entry.c.
 **********************************************************************************************************************/
/***********************************************************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2023 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/

#include "hal_data.h"
#include "ospi_b_ep.h"
#include "ospi_b_commands.h"

void fsp_assert(fsp_err_t err);

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event);
FSP_CPP_FOOTER

#define RESET_VALUE             (0x00)

/* Function declarations*/
void handle_error(fsp_err_t err,  const char *err_str);
void fsp_assert(fsp_err_t err);
void ospi_test_wait_until_wip(void);

uint8_t g_write_data[256];
uint8_t g_read_data[256];

/* External variables */
extern spi_flash_direct_transfer_t g_ospi_b_direct_transfer [OSPI_B_TRANSFER_MAX];

/*******************************************************************************************************************//**
 * main() is generated by the RA Configuration editor and is used to generate threads if an RTOS is used.  This function
 * is called by main() when no RTOS is used.
 **********************************************************************************************************************/
void hal_entry(void)
{
    /* TODO: add your own code here */
    fsp_err_t err = FSP_SUCCESS;
    spi_flash_direct_transfer_t transfer          = {RESET_VALUE};
    uint32_t                    flash_id          = RESET_VALUE;

    uint8_t *dest = (uint8_t*) 0x90001000U;

    for (uint32_t i = 0; i < sizeof(g_write_data); i++)
    {
        g_write_data[i] = (uint8_t) i;
    }

    /* Open OSPI module */
     err = R_OSPI_B_Open(&g_ospi_b_ctrl, &g_ospi_b_cfg);
     fsp_assert (err);

     /* Switch OSPI module to 1S-1S-1S mode to configure flash device */
     err = R_OSPI_B_SpiProtocolSet(&g_ospi_b_ctrl, SPI_FLASH_PROTOCOL_EXTENDED_SPI);
     fsp_assert (err);

     /* Reset flash device by driving OM_RESET pin */
     R_XSPI->LIOCTL_b.RSTCS0 = 0;
     R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_PULSE, OSPI_B_TIME_UNIT);
     R_XSPI->LIOCTL_b.RSTCS0 = 1;
     R_BSP_SoftwareDelay(OSPI_B_TIME_RESET_SETUP, OSPI_B_TIME_UNIT);


     err = ospi_b_read_device_id(&flash_id);
     fsp_assert (err);
     if(!((flash_id == 0x21A5BEF) || (flash_id == 0x0F1A5A34)))
     {
         __BKPT(0);
     }

     err = ospi_b_write_enable();
     fsp_assert (err);

      /* 写寄存器的数据，修改地址宽度为4*/
      transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_WITCH_SPI];
      err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
      fsp_assert (err);

      err = ospi_b_write_enable();
      fsp_assert (err);

      /*read flag Reg  查看地址宽度最低位 0-3 1-4*/
      transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_FLAG_SPI];
      err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
      fsp_assert (err);

      err = ospi_b_write_enable();
      fsp_assert (err);

      /* 读取寄存器的数据，查看通信模式  FFh: Default for Extended SPI*/
      transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_READ_VOLA_SPI];
      err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
      fsp_assert (err);

      /* Transfer write enable command */
      err = ospi_b_write_enable();
      fsp_assert (err);

      /* 全片擦除*/
      transfer = g_ospi_b_direct_transfer[OSPI_B_TRANSFER_WRITE_ERASE_SPI];
      err = R_OSPI_B_DirectTransfer(&g_ospi_b_ctrl, &transfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
      fsp_assert (err);


      ospi_b_read_status();

      err = ospi_b_write_enable();
      fsp_assert (err);

      /* 写入数据*/
      err = R_OSPI_B_Write(&g_ospi_b_ctrl, g_write_data, dest, 256);
      fsp_assert (err);

      err = ospi_b_wait_operation(OSPI_B_TIME_WRITE);
      fsp_assert (err);

      uint32_t        execute_time                = RESET_VALUE;
      err = ospi_b_read_operation (dest + 1, &execute_time);
      fsp_assert (err);

      for (uint32_t i = 0; i < sizeof(g_write_data); i++)
      {
          if (g_write_data[i] != g_read_data[i])
          {
              fsp_assert (FSP_ERR_ASSERTION);
          }
      }

      // happy path
      __BKPT(0);

#if BSP_TZ_SECURE_BUILD
    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif
}

/*******************************************************************************************************************//**
 * Wait until work in progress bit is cleared
 **********************************************************************************************************************/
void ospi_test_wait_until_wip(void)
{
    spi_flash_status_t status;
    status.write_in_progress = true;
    uint32_t timeout = UINT32_MAX;
    while ((status.write_in_progress) && (--timeout > 0))
    {
        fsp_assert (R_OSPI_B_StatusGet (&g_ospi_b_ctrl, &status));

    }

    if (0 == timeout)
    {
        fsp_assert (FSP_ERR_TIMEOUT);
    }
}

/*******************************************************************************************************************//**
 * This function is called at various points during the startup process.  This implementation uses the event that is
 * called right before main() to set up the pins.
 *
 * @param[in]  event    Where at in the start up process the code is currently at
 **********************************************************************************************************************/
void R_BSP_WarmStart(bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0

        /* Enable reading from data flash. */
        R_FACI_LP->DFLCTL = 1U;

        /* Would normally have to wait tDSTOP(6us) for data flash recovery. Placing the enable here, before clock and
         * C runtime initialization, should negate the need for a delay since the initialization will typically take more than 6us. */
#endif
    }

    if (BSP_WARM_START_POST_C == event)
    {
        /* C runtime environment and system clocks are setup. */

        /* Configure pins. */
        R_IOPORT_Open (&g_ioport_ctrl, g_ioport.p_cfg);
    }
}

void fsp_assert(fsp_err_t err)
{
    if (FSP_SUCCESS != err)
    {
        __BKPT(0);
    }
}

#if BSP_TZ_SECURE_BUILD

FSP_CPP_HEADER
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ();

/* Trustzone Secure Projects require at least one nonsecure callable function in order to build (Remove this if it is not required to build). */
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ()
{

}
FSP_CPP_FOOTER

#endif
