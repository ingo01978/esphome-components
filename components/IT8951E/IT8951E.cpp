#include "IT8941E.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace it8951e {

static const char *const TAG = "it8951e";


#define bcm2835_gpio_write digitalWrite
#define bcm2835_spi_transfer SPI.transfer
#define bcm2835_gpio_lev digitalRead

//Built in I80 Command Code
#define IT8951_TCON_SYS_RUN      0x0001
#define IT8951_TCON_STANDBY      0x0002
#define IT8951_TCON_SLEEP        0x0003
#define IT8951_TCON_REG_RD       0x0010
#define IT8951_TCON_REG_WR       0x0011
#define IT8951_TCON_MEM_BST_RD_T 0x0012
#define IT8951_TCON_MEM_BST_RD_S 0x0013
#define IT8951_TCON_MEM_BST_WR   0x0014
#define IT8951_TCON_MEM_BST_END  0x0015
#define IT8951_TCON_LD_IMG       0x0020
#define IT8951_TCON_LD_IMG_AREA  0x0021
#define IT8951_TCON_LD_IMG_END   0x0022

//I80 User defined command code
#define USDEF_I80_CMD_DPY_AREA     0x0034
#define USDEF_I80_CMD_GET_DEV_INFO 0x0302
#define USDEF_I80_CMD_DPY_BUF_AREA 0x0037
//Panel
#define IT8951_PANEL_WIDTH   1024 //it Get Device information
#define IT8951_PANEL_HEIGHT   758

//Rotate mode
#define IT8951_ROTATE_0     0
#define IT8951_ROTATE_90    1
#define IT8951_ROTATE_180   2
#define IT8951_ROTATE_270   3

//Pixel mode , BPP - Bit per Pixel
#define IT8951_2BPP   0
#define IT8951_3BPP   1
#define IT8951_4BPP   2
#define IT8951_8BPP   3

//Waveform Mode
#define IT8951_MODE_0   0
#define IT8951_MODE_1   1
#define IT8951_MODE_2   2
#define IT8951_MODE_3   3
#define IT8951_MODE_4   4
//Endian Type
#define IT8951_LDIMG_L_ENDIAN   0
#define IT8951_LDIMG_B_ENDIAN   1
//Auto LUT
#define IT8951_DIS_AUTO_LUT   0
#define IT8951_EN_AUTO_LUT    1
//LUT Engine Status
#define IT8951_ALL_LUTE_BUSY 0xFFFF

//-----------------------------------------------------------------------
// IT8951 TCon Registers defines
//-----------------------------------------------------------------------
//Register Base Address
#define DISPLAY_REG_BASE 0x1000               //Register RW access for I80 only
//Base Address of Basic LUT Registers
#define LUT0EWHR  (DISPLAY_REG_BASE + 0x00)   //LUT0 Engine Width Height Reg
#define LUT0XYR   (DISPLAY_REG_BASE + 0x40)   //LUT0 XY Reg
#define LUT0BADDR (DISPLAY_REG_BASE + 0x80)   //LUT0 Base Address Reg
#define LUT0MFN   (DISPLAY_REG_BASE + 0xC0)   //LUT0 Mode and Frame number Reg
#define LUT01AF   (DISPLAY_REG_BASE + 0x114)  //LUT0 and LUT1 Active Flag Reg
//Update Parameter Setting Register
#define UP0SR (DISPLAY_REG_BASE + 0x134)      //Update Parameter0 Setting Reg

#define UP1SR     (DISPLAY_REG_BASE + 0x138)  //Update Parameter1 Setting Reg
#define LUT0ABFRV (DISPLAY_REG_BASE + 0x13C)  //LUT0 Alpha blend and Fill rectangle Value
#define UPBBADDR  (DISPLAY_REG_BASE + 0x17C)  //Update Buffer Base Address
#define LUT0IMXY  (DISPLAY_REG_BASE + 0x180)  //LUT0 Image buffer X/Y offset Reg
#define LUTAFSR   (DISPLAY_REG_BASE + 0x224)  //LUT Status Reg (status of All LUT Engines)

#define BGVR      (DISPLAY_REG_BASE + 0x250)  //Bitmap (1bpp) image color table
//-------System Registers----------------
#define SYS_REG_BASE 0x0000

//Address of System Registers
#define I80CPCR (SYS_REG_BASE + 0x04)
//-------Memory Converter Registers----------------
#define MCSR_BASE_ADDR 0x0200
#define MCSR (MCSR_BASE_ADDR  + 0x0000)
#define LISAR (MCSR_BASE_ADDR + 0x0008)

#define SYS_REG_BASE 0x0000
#define I80CPCR (SYS_REG_BASE + 0x04)

#define USDEF_I80_CMD_GET_DEV_INFO 0x0302

#define IT8951_TCON_REG_WR       0x0011


void it8951e::setup_pins_() {
  this->init_internal_(this->get_buffer_length_());
//   this->cs_pin_->setup();  // OUTPUT
//   this->cs_pin_->digital_write(false);
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();  // OUTPUT
    this->reset_pin_->digital_write(true);
  }
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();  // INPUT
  }
  if (this->en_pin_ != nullptr) {
    this->en_pin_->setup();  // OUTPUT
    this->en_pin_->digital_write(true);
  }
  this->spi_setup();

  this->reset_();
}

void it8951e::initialize() {
  this->reset_();

  //Get Device Info
  this->GetIT8951SystemInfo();

  if (!this->gstI80DevInfo.usPanelW || !this->gstI80DevInfo.usPanelH) {
    return;
  }

  this->gulImgBufAddr = this->gstI80DevInfo.usImgBufAddrL | ((uint32_t)this->gstI80DevInfo.usImgBufAddrH << 16);

  //Set to Enable I80 Packed mode
  this->IT8951WriteReg(I80CPCR, 0x0001);
}

void it8951e::enablePower() { this->en_pin_->digital_write(true); }
void it8951e::disablePower() { this->en_pin_->digital_write(false); }

float it8951e::get_setup_priority() const { return setup_priority::PROCESSOR; }

void it8951e::update() {
  this->do_update_();
  this->display();
}
void it8951e::fill(Color color) {
  // flip logic
  const uint8_t fill = ((color.white << 4) & 0xF) | (color.white & 0xF);
  for (uint32_t i = 0; i < this->get_buffer_length_(); i++)
    this->buffer_[i] = fill;
}
void HOT it8951e::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= pstDevInfo->usPanelW || y >= pstDevInfo->usPanelH || x < 0 || y < 0)
    return;

  const uint32_t pos = (x + y * pstDevInfo->usPanelW) / 2u;
  const uint8_t subpos = x % 2;
  // flip logic
  this->buffer_[pos] = ((color.white << 4) & 0xF) >> subpos * 4;
}

void it8951e::display(){

  uint32_t i,j;
  //Source buffer address of Host
  uint16_t* pusFrameBuf = (uint16_t*)this->buffer_;

  IT8951DevInfo* pstDevInfo;
  pstDevInfo = (IT8951DevInfo*)this->gstI80DevInfo;

  IT8951LdImgInfo pstLdImgInfo;
  pstLdImgInfo.usEndianType = IT8951_LDIMG_L_ENDIAN; //little or Big Endian
  pstLdImgInfo.usPixelFormat = IT8951_4BPP; //bpp
  pstLdImgInfo.usRotate = IT8951_ROTATE_0; //Rotate mode
  pstLdImgInfo.ulStartFBAddr = self->buffer_; //Start address of source Frame buffer
  pstLdImgInfo.ulImgBufBaseAddr = pstDevInfo->usImgBufAddrL | (pstDevInfo->usImgBufAddrH << 16);//Base address of target image buffer
  IT8951AreaImgInfo pstAreaImgInfo;
  pstAreaImgInfo.usX = 0;
  pstAreaImgInfo.usY = 0;
  pstAreaImgInfo.usWidth = pstDevInfo->usPanelW;
  pstAreaImgInfo.usHeight = pstDevInfo->usPanelH;

  //Set Image buffer(IT8951) Base address
  this->IT8951SetImgBufBaseAddr(pstLdImgInfo.ulImgBufBaseAddr);
  //Send Load Image start Cmd
  this->IT8951LoadImgAreaStart(&pstLdImgInfo , &pstAreaImgInfo);
  //Host Write Data
  for(j=0;j< pstAreaImgInfo->usHeight;j++)
  {
     for(i=0;i< pstAreaImgInfo->usWidth/2;i++)
      {
          //Write a Word(2-Bytes) for each time
          this->LCDWriteData(*pusFrameBuf);
          pusFrameBuf++;
      }
  }
  //Send Load Img End Command
  this->IT8951LoadImgEnd();
}
uint32_t it8951e::get_buffer_length_() { return pstDevInfo->usPanelW * pstDevInfo->usPanelH; }
void it8951e::on_safe_shutdown() { this->deep_sleep(); }




void it8951e::GetIT8951SystemInfo()
{
  uint16_t* pusWord = (uint16_t*)this->gstI80DevInfo;
  IT8951DevInfo* pstDevInfo;

  //Send I80 CMD
  this->LCDWriteCmdCode(USDEF_I80_CMD_GET_DEV_INFO);
 
  //Burst Read Request for SPI interface only
  this->LCDReadNData(pusWord, sizeof(IT8951DevInfo)/2);//Polling HRDY for each words(2-bytes) if possible
  
  //Show Device information of IT8951
  pstDevInfo = (IT8951DevInfo*)this->gstI80DevInfo;
  ESP_LOGD(TAG, "Panel(W,H) = (%d,%d)\r\n",
  pstDevInfo->usPanelW, pstDevInfo->usPanelH );
  ESP_LOGD(TAG, "Image Buffer Address = %X\r\n",
  pstDevInfo->usImgBufAddrL | (pstDevInfo->usImgBufAddrH << 16));
  //Show Firmware and LUT Version
  ESP_LOGD(TAG, "FW Version = %s\r\n", (uint8_t*)pstDevInfo->usFWVersion);
  ESP_LOGD(TAG, "LUT Version = %s\r\n", (uint8_t*)pstDevInfo->usLUTVersion);
}

//-----------------------------------------------------------
//Host Cmd 10---LD_IMG
//-----------------------------------------------------------
void it8951e::IT8951LoadImgStart(IT8951LdImgInfo* pstLdImgInfo)
{
    uint16_t usArg;
    //Setting Argument for Load image start
    usArg = (pstLdImgInfo->usEndianType << 8 )
    |(pstLdImgInfo->usPixelFormat << 4)
    |(pstLdImgInfo->usRotate);
    //Send Cmd
    this->LCDWriteCmdCode(IT8951_TCON_LD_IMG);
    //Send Arg
    this->LCDWriteData(usArg);
}
//-----------------------------------------------------------
//Host Cmd 11---LD_IMG_AREA
//-----------------------------------------------------------
void it8951e::IT8951LoadImgAreaStart(IT8951LdImgInfo* pstLdImgInfo ,IT8951AreaImgInfo* pstAreaImgInfo)
{
    uint16_t usArg[5];
    //Setting Argument for Load image start
    usArg[0] = (pstLdImgInfo->usEndianType << 8 )
    |(pstLdImgInfo->usPixelFormat << 4)
    |(pstLdImgInfo->usRotate);
    usArg[1] = pstAreaImgInfo->usX;
    usArg[2] = pstAreaImgInfo->usY;
    usArg[3] = pstAreaImgInfo->usWidth;
    usArg[4] = pstAreaImgInfo->usHeight;
    //Send Cmd and Args
    this->LCDSendCmdArg(IT8951_TCON_LD_IMG_AREA , usArg , 5);
}
//-----------------------------------------------------------
//Host Cmd 12---LD_IMG_END
//-----------------------------------------------------------
void it8951e::IT8951LoadImgEnd(void)
{
    this->LCDWriteCmdCode(IT8951_TCON_LD_IMG_END);
}

//-----------------------------------------------------------
//Initial function 2---Set Image buffer base address
//-----------------------------------------------------------
void it8951e::IT8951SetImgBufBaseAddr(uint32_t ulImgBufAddr)
{
  uint16_t usWordH = (uint16_t)((ulImgBufAddr >> 16) & 0x0000FFFF);
  uint16_t usWordL = (uint16_t)( ulImgBufAddr & 0x0000FFFF);
  //Write LISAR Reg
  this->IT8951WriteReg(LISAR + 2 ,usWordH);
  this->IT8951WriteReg(LISAR ,usWordL);
}

//-----------------------------------------------------------
// 3.6. Display Functions
//-----------------------------------------------------------

//-----------------------------------------------------------
//Display function 1---Wait for LUT Engine Finish
//                     Polling Display Engine Ready by LUTNo
//-----------------------------------------------------------
void it8951e::IT8951WaitForDisplayReady()
{
  //Check IT8951 Register LUTAFSR => NonZero Busy, 0 - Free
  while(this->IT8951ReadReg(LUTAFSR));
}

//-----------------------------------------------------------
//Display function 2---Load Image Area process
//-----------------------------------------------------------
void it8951e::IT8951HostAreaPackedPixelWrite(IT8951LdImgInfo* pstLdImgInfo,IT8951AreaImgInfo* pstAreaImgInfo)
{
  uint32_t i,j;
  //Source buffer address of Host
  uint16_t* pusFrameBuf = (uint16_t*)pstLdImgInfo->ulStartFBAddr;

  //Set Image buffer(IT8951) Base address
  this->IT8951SetImgBufBaseAddr(pstLdImgInfo->ulImgBufBaseAddr);
  //Send Load Image start Cmd
  this->IT8951LoadImgAreaStart(pstLdImgInfo , pstAreaImgInfo);
  //Host Write Data
  for(j=0;j< pstAreaImgInfo->usHeight;j++)
  {
     for(i=0;i< pstAreaImgInfo->usWidth/2;i++)
      {
          //Write a Word(2-Bytes) for each time
          this->LCDWriteData(*pusFrameBuf);
          pusFrameBuf++;
      }
  }
  //Send Load Img End Command
  this->IT8951LoadImgEnd();
}

//-----------------------------------------------------------
//Display functions 3---Application for Display panel Area
//-----------------------------------------------------------
void it8951e::IT8951DisplayArea(uint16_t usX, uint16_t usY, uint16_t usW, uint16_t usH, uint16_t usDpyMode)
{
  //Send I80 Display Command (User defined command of IT8951)
  this->LCDWriteCmdCode(USDEF_I80_CMD_DPY_AREA); //0x0034
  //Write arguments
  this->LCDWriteData(usX);
  this->LCDWriteData(usY);
  this->LCDWriteData(usW);
  this->LCDWriteData(usH);
  this->LCDWriteData(usDpyMode);
}


//-----------------------------------------------------------
//Host controller function 1---Wait for host data Bus Ready
//-----------------------------------------------------------
void it8951e::LCDWaitForReady()
{
  uint8_t ulData = this->busy_pin_->digital_read();
  const uint32_t start = millis();
  while(ulData == 0)
  {
    if (millis() - start > this->idle_timeout_()) {
      ESP_LOGE(TAG, "Timeout while displaying image!");
      return;
    }
    ulData = this->busy_pin_->digital_read();
  }
}

//-----------------------------------------------------------
//Host controller function 2---Write command code to host data Bus
//-----------------------------------------------------------
void it8951e::LCDWriteCmdCode(uint16_t usCmdCode)
{
  //Set Preamble for Write Command
  uint16_t wPreamble = 0x6000; 
  
  this->LCDWaitForReady();  

  this->enable();
  
  this->write_byte(wPreamble>>8);
  this->write_byte(wPreamble);
  
  this->LCDWaitForReady();  
  
  this->write_byte(usCmdCode>>8);
  this->write_byte(usCmdCode);
  
  this->disable();
}

//-----------------------------------------------------------
//Host controller function 3---Write Data to host data Bus
//-----------------------------------------------------------
void it8951e::LCDWriteData(uint16_t usData)
{
  //Set Preamble for Write Data
  uint16_t wPreamble  = 0x0000;

  this->LCDWaitForReady();

  this->enable();

  this->write_byte(wPreamble>>8);
  this->write_byte(wPreamble);
  
  this->LCDWaitForReady();
      
  this->write_byte(usData>>8);
  this->write_byte(usData);
  
  this->disable();
}

void it8951e::LCDWriteNData(uint16_t* pwBuf, uint32_t ulSizeWordCnt)
{
  uint32_t i;

  uint16_t wPreamble  = 0x0000;

  this->LCDWaitForReady();

  this->enable();
  
  this->write_byte(wPreamble>>8);
  this->write_byte(wPreamble);
  
  this->LCDWaitForReady();

  for(i=0;i<ulSizeWordCnt;i++)
  {
    this->write_byte(pwBuf[i]>>8);
    this->write_byte(pwBuf[i]);
  }
  
  this->disable();
}


//-----------------------------------------------------------
//Host controller function 4---Read Data from host data Bus
//-----------------------------------------------------------
uint16_t it8951e::LCDReadData()
{
  uint16_t wRData; 
  
  uint16_t wPreamble = 0x1000;

  this->LCDWaitForReady();

  this->enable();
    
  this->write_byte(wPreamble>>8);
  this->write_byte(wPreamble);

  this->LCDWaitForReady();
  
  wRData=this->read_byte();
  wRData=this->read_byte();
  
  this->LCDWaitForReady();
  
  wRData = this->read_byte()<<8;
  wRData |= this->read_byte();
    
  this->disable();
    
  return wRData;
}

//-----------------------------------------------------------
//  Read Burst N words Data
//-----------------------------------------------------------
void it8951e::LCDReadNData(uint16_t* pwBuf, uint32_t ulSizeWordCnt)
{
  uint32_t i;
  
  uint16_t wPreamble = 0x1000;

  this->LCDWaitForReady();

  this->enable();
    
  this->write_byte(wPreamble>>8);
  this->write_byte(wPreamble);

  this->LCDWaitForReady();
  
  pwBuf[0]=this->read_byte();
  pwBuf[0]=this->read_byte();
  
  this->LCDWaitForReady();
  
  for(i=0;i<ulSizeWordCnt;i++)
  {
    pwBuf[i] = this->read_byte()<<8;
    pwBuf[i] |= this->read_byte();
  }
  
  this->disable();
}

//-----------------------------------------------------------
//Host controller function 5---Write command to host data Bus with aruments
//-----------------------------------------------------------
void it8951e::LCDSendCmdArg(uint16_t usCmdCode,uint16_t* pArg, uint16_t usNumArg)
{
     uint16_t i;
     //Send Cmd code
     this->LCDWriteCmdCode(usCmdCode);
     //Send Data
     for(i=0;i<usNumArg;i++)
     {
         this->LCDWriteData(pArg[i]);
     }
}



//-----------------------------------------------------------
//Host Cmd 4---REG_RD
//-----------------------------------------------------------
uint16_t it8951e::IT8951ReadReg(uint16_t usRegAddr)
{
  uint16_t usData;
  
  //Send Cmd and Register Address
  this->LCDWriteCmdCode(IT8951_TCON_REG_RD);
  this->LCDWriteData(usRegAddr);
  //Read data from Host Data bus
  usData = LCDReadData();
  return usData;
}
//-----------------------------------------------------------
//Host Cmd 5---REG_WR
//-----------------------------------------------------------
void it8951e::IT8951WriteReg(uint16_t usRegAddr,uint16_t usValue)
{
  //Send Cmd , Register Address and Write Value
  this->LCDWriteCmdCode(IT8951_TCON_REG_WR);
  this->LCDWriteData(usRegAddr);
  this->LCDWriteData(usValue);
}

}  // namespace it8951e
}  // namespace esphome