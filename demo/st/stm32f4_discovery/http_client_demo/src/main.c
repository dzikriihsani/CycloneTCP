/**
 * @file main.c
 * @brief Main routine
 *
 * @section License
 *
 * Copyright (C) 2010-2013 Oryx Embedded. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded (www.oryx-embedded.com)
 * @version 1.3.8
 **/

//Dependencies
#include <stdlib.h>
#include "stm32f4xx.h"
#include "stm32f4_discovery.h"
#include "stm32f4_discovery_lcd.h"
#include "os.h"
#include "tcp_ip_stack.h"
#include "stm32f4x7_eth.h"
#include "lan8720.h"
#include "dhcp_client.h"
#include "debug.h"

//Constant definitions
#define SERVER_NAME "www.oryx-embedded.com"
#define SERVER_PORT 80
#define REQUEST_URI "/test.php"

//Global variables
uint_t lcdLine = 0;
uint_t lcdColumn = 0;


/**
 * @brief Set cursor location
 * @param[in] line Line number
 * @param[in] column Column number
 **/

void lcdSetCursor(uint_t line, uint_t column)
{
   lcdLine = min(line, 10);
   lcdColumn = min(column, 20);
}


/**
 * @brief Write a character to the LCD display
 * @param[in] c Character to be written
 **/

void lcdPutChar(char_t c)
{
   if(c == '\r')
   {
      lcdColumn = 0;
   }
   else if(c == '\n')
   {
      lcdColumn = 0;
      lcdLine++;
   }
   else if(lcdLine < 10 && lcdColumn < 20)
   {
      //Display current character
      LCD_DisplayChar(lcdLine * 24, lcdColumn * 16, c);

      //Advance the cursor position
      if(++lcdColumn >= 20)
      {
         lcdColumn = 0;
         lcdLine++;
      }
   }
}


/**
 * @brief HTTP client test routine
 * @return Error code
 **/

error_t httpClientTest(void)
{
   error_t error;
   size_t length;
   IpAddr ipAddr;
   Socket *socket;
   static char_t buffer[256];

   //Debug message
   TRACE_INFO("\r\n\r\nResolving server name...\r\n");

   //Resolve HTTP server name
   error = getHostByName(NULL, SERVER_NAME, &ipAddr, 1, NULL, 0);
   //Any error to report?
   if(error)
   {
      //Debug message
      TRACE_INFO("Failed to resolve server name!\r\n");
      //Exit immedialtely
      return error;
   }

   //Create a new socket to handle the request
   socket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
   //Any error to report?
   if(!socket)
   {
      //Debug message
      TRACE_INFO("Failed to open socket!\r\n");
      //Exit immedialtely
      return error;
   }

   //Start of exception handling block
   do
   {
      //Debug message
      TRACE_INFO("Connecting to HTTP server %s\r\n", ipAddrToString(&ipAddr, NULL));

      //Connect to the HTTP server
      error = socketConnect(socket, &ipAddr, SERVER_PORT);
      //Any error to report?
      if(error) break;

      //Debug message
      TRACE_INFO("Successful connection\r\n");

      //Format HTTP request
      length = sprintf(buffer, "GET %s HTTP/1.0\r\nHost: %s:%u\r\n\r\n",
         REQUEST_URI, SERVER_NAME, SERVER_PORT);

      //Debug message
      TRACE_INFO("\r\nHTTP request:\r\n%s", buffer);

      //Send HTTP request
      error = socketSend(socket, buffer, length, NULL, 0);
      //Any error to report?
      if(error) break;

      //Debug message
      TRACE_INFO("HTTP response header:\r\n");

      //Parse HTTP response header
      while(1)
      {
         //Read the header line by line
         error = socketReceive(socket, buffer, sizeof(buffer) - 1, &length, SOCKET_FLAG_BREAK_CRLF);
         //End of stream?
         if(error) break;

         //Properly terminate the string with a NULL character
         buffer[length] = '\0';
         //Dump current data
         TRACE_INFO("%s", buffer);

         //The end of the header has been reached?
         if(!strcmp(buffer, "\r\n"))
            break;
      }

      //Debug message
      TRACE_INFO("HTTP response body:\r\n");

      //Parse HTTP response body
      while(1)
      {
         //Read response body
         error = socketReceive(socket, buffer, sizeof(buffer) - 1, &length, 0);
         //End of stream?
         if(error) break;

         //Properly terminate the string with a NULL character
         buffer[length] = '\0';
         //Dump current data
         TRACE_INFO("%s", buffer);
      }

      //End of exception handling block
   } while(0);

   //Close the connection
   socketClose(socket);
   //Debug message
   TRACE_INFO("\r\nConnection closed...\r\n");

   //Return status code
   return error;
}


/**
 * @brief User task
 **/

void userTask(void *param)
{
   char_t buffer[40];

   //Point to the network interface
   NetInterface *interface = &netInterface[0];

   //Display IPv4 address
   lcdSetCursor(2, 0);
   printf("IPv4 Addr");
   lcdSetCursor(5, 0);
   printf("Press user button\r\nto run test");

   //Endless loop
   while(1)
   {
      //Refresh IPv4 address
      lcdSetCursor(3, 0);
      printf("%-16s", ipv4AddrToString(interface->ipv4Config.addr, buffer));

      //User button pressed?
      if(STM_EVAL_PBGetState(BUTTON_USER))
      {
         //HTTP client test routine
         httpClientTest();

         //Wait for the user button to be released
         while(STM_EVAL_PBGetState(BUTTON_USER));
      }

      //100ms delay
      osDelay(100);
   }
}


/**
 * @brief LED blinking task
 **/

void blinkTask(void *parameters)
{
   while(1)
   {
      STM_EVAL_LEDOn(LED4);
      osDelay(100);
      STM_EVAL_LEDOff(LED4);
      osDelay(900);
   }
}


/**
 * @brief Main entry point
 * @return Unused value
 **/

int_t main(void)
{
   error_t error;
   NetInterface *interface;
   OsTask *task;

   static DhcpClientSettings dhcpClientSettings;
   static DhcpClientCtx dhcpClientContext;

   //Configure debug UART
   debugInit(115200);

   //Start-up message
   TRACE_INFO("\r\n");
   TRACE_INFO("***********************************\r\n");
   TRACE_INFO("*** CycloneTCP HTTP Client Demo ***\r\n");
   TRACE_INFO("***********************************\r\n");
   TRACE_INFO("Copyright: 2010-2013 Oryx Embedded\r\n");
   TRACE_INFO("Compiled: %s %s\r\n", __DATE__, __TIME__);
   TRACE_INFO("Target: STM32F407\r\n");
   TRACE_INFO("\r\n");

   //LED configuration
   STM_EVAL_LEDInit(LED3);
   STM_EVAL_LEDInit(LED4);
   STM_EVAL_LEDInit(LED5);
   STM_EVAL_LEDInit(LED6);

   //Clear LEDs
   STM_EVAL_LEDOff(LED3);
   STM_EVAL_LEDOff(LED4);
   STM_EVAL_LEDOff(LED5);
   STM_EVAL_LEDOff(LED6);

   //Initialize user button
   STM_EVAL_PBInit(BUTTON_USER, BUTTON_MODE_GPIO);

   //Initialize LCD display
   STM32f4_Discovery_LCD_Init();
   LCD_SetBackColor(Blue);
   LCD_SetTextColor(White);
   LCD_SetFont(&Font16x24);
   LCD_Clear(Blue);

   //Welcome message
   lcdSetCursor(0, 0);
   printf("HTTP Client Demo");

   //TCP/IP stack initialization
   error = tcpIpStackInit();

   //Any error to report?
   if(error)
   {
      //Debug message
      TRACE_ERROR("Failed to initialize TCP/IP stack!\r\n");
   }

   //Configure the first Ethernet interface
   interface = &netInterface[0];
   //Select the relevant network adapter
   interface->nicDriver = &stm32f4x7EthDriver;
   interface->phyDriver = &lan8720PhyDriver;
   //Interface name
   strcpy(interface->name, "eth0");
   //Set host MAC address
   macStringToAddr("00-AB-CD-EF-04-07", &interface->macAddr);

#if (IPV6_SUPPORT == ENABLED)
   //Set link-local IPv6 address
   ipv6StringToAddr("fe80::00ab:cdef:0407", &interface->ipv6Config.linkLocalAddr);
#endif

   //Initialize network interface
   error = tcpIpStackConfigInterface(interface);

   //Any error to report?
   if(error)
   {
      //Debug message
      TRACE_ERROR("Failed to configure interface %s!\r\n", interface->name);
   }

#if 1
   //Set the network interface to be configured by DHCP
   dhcpClientSettings.interface = &netInterface[0];
   //Disable rapid commit option
   dhcpClientSettings.rapidCommit = FALSE;
   //Start DHCP client
   error = dhcpClientStart(&dhcpClientContext, &dhcpClientSettings);

   //Failed to start DHCP client?
   if(error)
   {
      //Debug message
      TRACE_ERROR("Failed to start DHCP client!\r\n");
   }
#else
   //Manual configuration
   interface = &netInterface[0];

   //IPv4 address
   ipv4StringToAddr("192.168.0.20", &interface->ipv4Config.addr);
   //Subnet mask
   ipv4StringToAddr("255.255.255.0", &interface->ipv4Config.subnetMask);
   //Default gateway
   ipv4StringToAddr("192.168.0.254", &interface->ipv4Config.defaultGateway);

   //Primary and secondary DNS servers
   interface->ipv4Config.dnsServerCount = 2;
   ipv4StringToAddr("212.27.40.240", &interface->ipv4Config.dnsServer[0]);
   ipv4StringToAddr ("212.27.40.241", &interface->ipv4Config.dnsServer[1]);
#endif

   //Create user task
   task = osTaskCreate("User Task", userTask, NULL, 500, 1);
   //Failed to create the task?
   if(task == OS_INVALID_HANDLE)
   {
      //Debug message
      TRACE_ERROR("Failed to create task!\r\n");
   }

   //Create a task to blink the LED
   task = osTaskCreate("Blink", blinkTask, NULL, 500, 1);
   //Failed to create the task?
   if(task == OS_INVALID_HANDLE)
   {
      //Debug message
      TRACE_ERROR("Failed to create task!\r\n");
   }

   //Start the execution of tasks
   osStart();

   //This function should never return
   return 0;
}
