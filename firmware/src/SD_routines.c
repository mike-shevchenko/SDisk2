/*------------------------------------

SDISK II LCD Firmware

2010.11.11 by Koichi Nishida
2012.01.26 by F�bio Belavenuto
2015.07.02 by Alexandre Suaide

-------------------------------------
*/

/*
2015.07.02 by Alexandre Suaide
Added support for SDHC cards and subdirectories
Removed DSK to NIC conversion
FAT16 and FAT32 disks should have at least 64 blocks per cluster
*/

/*
2012.01.26 by F�bio Belavenuto
Added support for image exchange using a button added in the Brazilian version by Victor Trucco
Added support for a 16x2 LCD
*/

/*
This is a part of the firmware for DISK II emulator by Nishida Radio.

Copyright (C) 2010 Koichi NISHIDA
email to Koichi NISHIDA: tulip-house@msf.biglobe.ne.jp

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "SPI_routines.h"
#include "SD_routines.h"

unsigned long lastBlockRead;
unsigned char SD_version, SDHC_flag,  *buffer, SD_speed;
unsigned char ch;
unsigned char res;
unsigned char count;
unsigned char shift;

#define NFF 12

unsigned char SD_init(void)
{
	
	errorCode = 0;
	
		unsigned char i, response;
		unsigned int retry = 0 ;
		SD_speed = 5;
		
		SPI_init();
		SPI_slow();
		if(SD_ejected()) return 0;
		
		SD_led_on();
		SD_unselect_card();
		lastBlockRead = 9999;
		for(i = 0; i < 100; ++i)  SPI_clock_pulse_slow();
		SD_select_card();

		for(i=0;i<NFF;i++) SPI_transmit (0xff);
		
		do
		{
			response = SD_sendCommand(CMD0, 0); //send 'reset & go idle' command
			retry++;
			if (retry>0x7ff)
			{
				SD_led_off();
				SD_unselect_card();
				errorCode = 1;
				return 0;   //time out, card not detected
			}
		} while (response != 0x01);
		
		for(i=0;i<NFF;i++) SPI_transmit (0xff);
		
		retry = 0;
		 
		unsigned long ARG = 0;;
		//this may change after checking the next command
		do
		{
			response = SD_sendCommand(CMD8,0x1AA); //Check power supply status, mandatory for SDHC card
			retry++;
			if((response & 0xFF) == 0xAA)
			{
				SD_version = SD_RAW_SPEC_2;
				ARG = 0x40000000;
				break;
			}
			if(response == 0x01)
			{
				SD_version = SD_RAW_SPEC_2;
				//SD_version = SD_RAW_SPEC_SDHC;
				ARG = 0x40000000;
				break;
			}
			if(retry>512)
			{
				SD_version = SD_RAW_SPEC_1;
				ARG = 0;
				break;
			} //time out
		} while (((response&0xFF) != 0xAA) && response != 0x01);

		for(i=0;i<NFF;i++) SPI_transmit (0xff);
		
		retry = 0;
		do
		{
			response = SD_sendCommand(CMD55,0); //CMD55, must be sent before sending any ACMD command
			response = SD_sendCommand(CMD41,ARG); //ACMD41
			retry++;
			if(retry > 512)	
			{
				errorCode = 2;
				SD_led_off();
				SD_unselect_card();
				return 0;
			}
		} while (response != 0x00);
		
		retry = 0;
		SDHC_flag = 0;
		shift = 9;
		
		if (SD_version == SD_RAW_SPEC_2)
		{		
			do
			{
				SPI_transmit (0xff);
				response = SD_sendCommand(CMD58,0); // check for SDHC
				retry++;
				if(retry>100)
				{
					break;
				} 
				
			} while (response != 0x00);
		}
		
		SD_sendCommand(CRC_ON_OFF, 0); //disable CRC; deafault - CRC disabled in SPI mode
		SD_setBlockSize(512); //set block size to 512; default size is 512
		SD_led_off();
		SD_unselect_card();
		return 1; //successful return
}

unsigned char SD_getRespFast(void)
{
	unsigned char ch;
	do 
	{
		ch = SPI_read_byte_fast();
		//if (SD_ejected()) return 0xff;
	} 
	while ((ch & 0x80) != 0);
	return ch;
}
void SD_cmdFast(unsigned char cmd, unsigned long adr)
{
	//adr = ((SDHC_flag==0) ? adr << 9 : adr);
	adr = (adr << shift);
	
	SPI_send_byte_fast(0xff);
	SPI_send_byte_fast(0x40 | cmd);
	SPI_send_byte_fast(adr >> 24);
	SPI_send_byte_fast((adr >> 16) & 0xff);
	SPI_send_byte_fast((adr >> 8) & 0xff);
	SPI_send_byte_fast(adr & 0xff);
	SPI_send_byte_fast(0x95);
	SPI_send_byte_fast(0xff);
	
	do { /*nop();*/ } while (((res=SD_getRespFast()) != 0) && (res != 0xff));
}

void SD_CMD17Special(unsigned long adr)
{
	//count = SD_speed;
	for(count = SD_speed; count>0; count --){}	
	SD_cmdFast(READ_SINGLE_BLOCK, adr);
	do
	{		
		ch = SPI_read_byte_fast();
		//nop(); nop(); nop(); nop();  nop();
		//if (SD_ejected()) return;
		//for(count = SD_speed; count>0; count --){}		
	}
	while(ch!=0xfe);
}

unsigned char SD_sendCommand(unsigned char cmd, unsigned long arg)
{
	unsigned char response, retry=0, status;
	
	//SD card accepts byte address while SDHC accepts block address in multiples of 512
	//so, if it's SD card we need to convert block address into corresponding byte address by
	//multipying it with 512. which is equivalent to shifting it left 9 times
	//following 'if' loop does that
	
	if(SDHC_flag == 0)
	if(cmd == READ_SINGLE_BLOCK     ||
	cmd == READ_MULTIPLE_BLOCKS  ||
	cmd == WRITE_SINGLE_BLOCK    ||
	cmd == WRITE_MULTIPLE_BLOCKS ||
	cmd == ERASE_BLOCK_START_ADDR||
	cmd == ERASE_BLOCK_END_ADDR )
	{
		arg = arg << 9;
	}
	
	//SD_select_card();
	
	SPI_transmit(0xFF);
	SPI_transmit(cmd | 0x40); //send command, first two bits always '01'
	SPI_transmit(arg>>24);
	SPI_transmit((arg>>16));
	SPI_transmit((arg>>8));
	SPI_transmit(arg);
	
	if(cmd == SEND_IF_COND)	 //it is compulsory to send correct CRC for CMD8 (CRC=0x87) & CMD0 (CRC=0x95)
	  SPI_transmit(0x87);    //for remaining commands, CRC is ignored in SPI mode
	else
	  SPI_transmit(0x95);
	
	while((response = SPI_receive()) == 0xff) //wait response
	  if(retry++ > 0xfe) break; //time out error
	
	if(response == 0x00 && cmd == CMD58)  //checking response of CMD58
	{
		status = SPI_receive() & 0x40;     //first byte of the OCR register (bit 31:24)
		shift = 0;
		if(status == 0x40) {SDHC_flag = 1; shift = 0; SD_version=SD_RAW_SPEC_SDHC;}  //we need it to verify SDHC card
		else { SDHC_flag = 0; shift = 9; SD_version = SD_RAW_SPEC_2;}
		
		SPI_receive(); //remaining 3 bytes of the OCR register are ignored here
		SPI_receive(); //one can use these bytes to check power supply limits of SD
		SPI_receive();
	}
	
	SPI_receive(); //extra 8 CLK
	//SD_unselect_card();
	
	return response; //return state
}

unsigned char SD_readSingleBlock(unsigned long startBlock)
{
	unsigned char response;
	unsigned int i, retry=0;
	
	if(startBlock==lastBlockRead) return 1;
	lastBlockRead = startBlock;
	
	SD_select_card();
	SPI_transmit (0xff);
	SPI_transmit (0xff);

	response = SD_sendCommand(READ_SINGLE_BLOCK, startBlock); 
	if(response != 0x00) return response; 
	
	//SD_select_card();
	retry = 0;
	while(SPI_receive() != 0xfe) if(retry++ > 0xfffe) {SD_unselect_card(); return 0;} 
	
	for(i=0; i<512; i++)  buffer[i] = SPI_receive(); 
	
	SPI_receive(); 
	SPI_receive();
	SPI_receive(); 
	SD_unselect_card();
	
	return 1;
}

unsigned char SD_writeSingleBlock(unsigned long startBlock)
{
	unsigned char response;
	unsigned int i, retry=0;
	
	response = SD_sendCommand(WRITE_SINGLE_BLOCK, startBlock); 
	if(response != 0x00) return response; 
	
	SD_select_card();
	SPI_transmit(0xfe);     
	
	for(i=0; i<512; i++)  SPI_transmit(buffer[i]); 
	
	SPI_transmit(0xff);     
	SPI_transmit(0xff);
	
	response = SPI_receive();
	if( (response & 0x1f) != 0x05) 
	{                             
		SD_unselect_card();              
		return response;
	}
	
	while(!SPI_receive()) 
	if(retry++ > 0xfffe) {SD_unselect_card(); return 0;}
	
	SD_unselect_card();
	SPI_transmit(0xff);       
	SD_select_card();         
	
	while(!SPI_receive()) 
	if(retry++ > 0xfffe) {SD_unselect_card(); return 0;}
	SD_unselect_card();
	
	return 1;
}
unsigned char SD_setBlockSize(unsigned long blockSize)
{
	return SD_sendCommand(SET_BLOCK_LEN, blockSize);
}
void SD_led_on()
{
	SD_LED_PORT = (1 <<SD_LED);
}

void SD_led_off()
{
	SD_LED_PORT = (0 <<SD_LED);
}
void SD_wait_finish()
{
	unsigned char ch;
	do
	{
		ch = SPI_read_byte_fast();
		if (SD_ejected()) return;
	} while (ch != 0xff);
}

