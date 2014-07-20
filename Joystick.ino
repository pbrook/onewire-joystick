/*
  Joystick emulation
 */
#include <debugf.h>

#include <Platform.h>
#include <USBAPI.h>
#include <USBDesc.h>
 
#include <SPI.h>
#include <TimerOne.h>
#include <EEPROM.h>
#include <ctype.h>
#include <owled.h>

OWLed led_player1(15, true);
OWLed led_player2(16, true);

u8 _hid_protocol = 1;
u8 _hid_idle = 1;

static volatile uint32_t button_raw_state;
static volatile uint32_t button_changed;
static uint32_t button_state;

#define LINE_BUF_LEN 64
char line_buf[LINE_BUF_LEN + 1];
char *line_ptr;

#define LATCH_PIN 14

#define BUTTON_1L 0x01u
#define BUTTON_1R 0x02u
#define BUTTON_1U 0x04u
#define BUTTON_1D 0x08u
#define BUTTON_2L 0x10u
#define BUTTON_2R 0x20u
#define BUTTON_2U 0x40u
#define BUTTON_2D 0x80u

static void
timer_fn(void)
{
  uint32_t newval;
  uint8_t bits;

  digitalWrite(LATCH_PIN, 0);
  delayMicroseconds(1);
  digitalWrite(LATCH_PIN, 1);
  bits = ~SPI.transfer(0);
  newval = bits;
  bits = ~SPI.transfer(0);
  newval |= (uint32_t)bits << 8;
  bits = ~SPI.transfer(0);
  newval |= (uint32_t)bits << 16;
  bits = ~SPI.transfer(0);
  newval |= (uint32_t)bits << 24;
  button_changed |= (button_raw_state ^ newval);
  button_raw_state = newval;
}

static int
from_hex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c + 10 - 'A';
  if (c >= 'a' && c <= 'f')
    return c + 10 - 'a';
  return -1;
}

#define NUM_PLAYER_LEDS 6

typedef struct
{
  unsigned r:4;
  unsigned g:4;
  unsigned b:4;
} led_color;

static led_color led_state[2][NUM_PLAYER_LEDS];

#define LED_RING_SIZE 16 // must be power-of-two. No overflow checking.
#define LED_RING_MASK (LED_RING_SIZE - 1)
static uint8_t led_update_ring[LED_RING_SIZE];
static uint8_t led_update_ptr;

static void
push_led(int player, int addr)
{
  int n;
  int p;

  n = addr | (player << 4);
  p = led_update_ptr;
  while (led_update_ring[p] != 0xff)
    {
      if (led_update_ring[p] == n)
	return;
      p = (p + 1) & LED_RING_MASK;
    }
  led_update_ring[p] = n;
  p = (p + 1) & LED_RING_MASK;
  led_update_ring[p] = 0xff;
}

static void
process_line(char *cmd)
{
  int player;
  int addr;
  int r;
  int g;
  int b;

  if (cmd[0] != 'L')
    return;

  if (strlen(cmd) != 6)
    return;

  player = from_hex(cmd[1]);
  addr = from_hex(cmd[2]);
  r = from_hex(cmd[3]);
  g = from_hex(cmd[4]);
  b = from_hex(cmd[5]);
  if (player < 0 || player > 1)
    return;
  if (addr < 0 || addr > 5 || r < 0 || g < 0 || b < 0)
    return;
  led_state[player][addr].r = r;
  led_state[player][addr].g = g;
  led_state[player][addr].b = b;
  push_led(player, addr);
  return;
}

static void
serial_add(char c)
{
  if (c == '\r' || c == '\n')
    {
      *line_ptr = 0;
      line_ptr = line_buf;
      if (line_buf[0])
	process_line(line_buf);
      return;
    }
  if (line_ptr == line_buf + LINE_BUF_LEN)
    return;
  *(line_ptr++) = c;
}

static uint8_t
button_to_axis(uint32_t left_mask, uint32_t right_mask)
{
  if (button_state & left_mask)
    return -0x7f;
  if (button_state & right_mask)
    return 0x7f;
  return 0;
}

u8 USB_SendSpace(u8);

static void
process_buttons(void)
{
  uint32_t new_state;
  uint32_t changed;
  uint32_t raw_state;
  uint8_t report[7];

  // Hack to workaround shitty Arduino USB code.
  // If we can't send state immediately then don't bother.
  if (USB_SendSpace(HID_TX) == 0)
    return;

  // Disable interrupts so the timer interrupt doesn'ti
  // change things underneath us.
  noInterrupts();
  changed = button_changed;
  button_changed = 0;
  raw_state = button_raw_state;
  interrupts();
  new_state = button_state;

  // A change event always causes a change in reported state,
  // even if it has already changed back.  On the next report we will
  // look for unchanged bits that do not match reported state
  new_state ^= changed;
  changed = (new_state ^ raw_state) & ~changed;
  new_state ^= changed;

  if (new_state == button_state && _hid_idle == 0)
    return;

  button_state = new_state;
  // Build report descriptor
  report[0] = (button_state >> 8) & 0xff;
  report[1] = (button_state >> 16) & 0xff;
  report[2] = (button_state >> 24) & 0xff;
  report[3] = button_to_axis(BUTTON_1L, BUTTON_1R);
  report[4] = button_to_axis(BUTTON_1U, BUTTON_1D);
  report[5] = button_to_axis(BUTTON_2L, BUTTON_2R);
  report[6] = button_to_axis(BUTTON_2U, BUTTON_2D);
  HID_SendReport(1, report, 7);
}

static void
update_led(void)
{
  OWLed *led;
  int player;
  int addr;
  led_color *color;

  addr = led_update_ring[led_update_ptr];
  if (addr == 0xff)
    return;

  led_update_ptr = (led_update_ptr + 1) & LED_RING_MASK;
  player = addr >> 4;
  addr &= 0xf;
  if (player == 0)
    led = &led_player1;
  else
    led = &led_player2;
  color = &led_state[player][addr];
  led->SetColor(addr, color->r << 4, color->g << 4, color->b << 4);
}

void loop()
{
  process_buttons();
  update_led();
  if (Serial.available())
    serial_add(Serial.read());
  delay(1);
}

static void
init_buttons(void)
{
  SPI.setDataMode(SPI_MODE2);
  SPI.begin();
  pinMode(LATCH_PIN, OUTPUT);
  digitalWrite(LATCH_PIN, 1);
  button_raw_state = 0;
  button_changed = 0;
  button_state = 0;
}

static void
init_leds()
{
  int i;

  delay(100);
  for (i = 0; i < 16; i++)
    {
      led_player1.SetColor(i, 0xff, 0xff, 0xff);
      led_player2.SetColor(i, 0xff, 0xff, 0xff);
    }
  delay(200);
  for (i = 0; i < 16; i++)
    {
      led_player1.SetColor(i, 0, 0, 0);
      led_player2.SetColor(i, 0, 0, 0);
    }
  led_update_ring[0] = 0xff;
}

void
setup()
{
  Serial.begin(9600);
  line_ptr = line_buf;
  init_buttons();

  Timer1.initialize();
  Timer1.attachInterrupt(timer_fn, 1000);

  init_leds();
}

//	HID report descriptor

#define LSB(_x) ((_x) & 0xFF)
#define MSB(_x) ((_x) >> 8)

static const u8 _hidReportDescriptor[] PROGMEM = {
    //	Joystick
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x04,                    // USAGE (Joystick)
    0xa1, 0x01,                    // COLLECTION (Application)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x85, 0x01,                    //     REPORT_ID (1)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x18,                    //     USAGE_MAXIMUM (Button 24)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x18,                    //     REPORT_COUNT (24)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x04,                    //     REPORT_COUNT (3)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0xc0,                          //   END_COLLECTION
    0xc0,                          // END_COLLECTION
};

static const HIDDescriptor _hidInterface PROGMEM =
{
	D_INTERFACE(HID_INTERFACE,1,3,0,0),
	D_HIDREPORT(sizeof(_hidReportDescriptor)),
	D_ENDPOINT(USB_ENDPOINT_IN (HID_ENDPOINT_INT),USB_ENDPOINT_TYPE_INTERRUPT,USB_EP_SIZE,0x01)
};

//================================================================================
//================================================================================
//	Driver

/* Have to override everything to avoid pulling in any of the core HID.c */
int HID_GetInterface(u8* interfaceNum)
{
	interfaceNum[0] += 1;	// uses 1
	return USB_SendControl(TRANSFER_PGM,&_hidInterface,sizeof(_hidInterface));
}

int HID_GetDescriptor(int i)
{
	return USB_SendControl(TRANSFER_PGM,_hidReportDescriptor,sizeof(_hidReportDescriptor));
}

void HID_SendReport(u8 id, const void* data, int len)
{
	USB_Send(HID_TX, &id, 1);
	USB_Send(HID_TX | TRANSFER_RELEASE,data,len);
}

bool HID_Setup(Setup& setup)
{
	u8 r = setup.bRequest;
	u8 requestType = setup.bmRequestType;
	if (REQUEST_DEVICETOHOST_CLASS_INTERFACE == requestType)
	{
		if (HID_GET_REPORT == r)
		{
			//HID_GetReport();
			return true;
		}
		if (HID_GET_PROTOCOL == r)
		{
			//Send8(_hid_protocol);	// TODO
			return true;
		}
	}
	
	if (REQUEST_HOSTTODEVICE_CLASS_INTERFACE == requestType)
	{
		if (HID_SET_PROTOCOL == r)
		{
			_hid_protocol = setup.wValueL;
			return true;
		}

		if (HID_SET_IDLE == r)
		{
			_hid_idle = setup.wValueL;
			return true;
		}
	}
	return false;
}
