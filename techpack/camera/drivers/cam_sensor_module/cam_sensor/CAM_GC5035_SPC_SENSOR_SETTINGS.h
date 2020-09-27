/******************** GC5035_OTP_EDIT_START*******************/
/*index 0 otp read init setting*/
{
    .reg_setting =
    {
        {.reg_addr = 0xfc, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf4, .reg_data = 0x40, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf5, .reg_data = 0xe9, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf6, .reg_data = 0x14, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf8, .reg_data = 0x49, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf9, .reg_data = 0x82, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfa, .reg_data = 0x10, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfc, .reg_data = 0x81, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x36, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xd3, .reg_data = 0x87, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x36, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf7, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfc, .reg_data = 0x8f, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfc, .reg_data = 0x8f, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfc, .reg_data = 0x8e, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x02, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x55, .reg_data = 0x84, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x65, .reg_data = 0x7e, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x66, .reg_data = 0x03, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0xc0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x68, .reg_data = 0x11, .delay = 0, .data_mask = 0x0},
    },
    .size = 22,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/*index 1 dpc page 0*/
{
    .reg_setting =
    {
		{.reg_addr = 0xe0, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0xf0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf3, .reg_data = 0x10, .delay = 0, .data_mask = 0x0},
    },
    .size = 3,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/*index 2 close read*/
{
    .reg_setting =
    {
        {.reg_addr = 0x67, .reg_data = 0xc0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf3, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
    },
    .size = 2,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/*index 3 dpc update setting1*/
{
    .reg_setting =
    {
        {.reg_addr = 0xfa, .reg_data = 0x10, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x02, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0xc0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xbe, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xa9, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x09, .reg_data = 0x33, .delay = 0, .data_mask = 0x0},
    },
    .size = 6,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 0,
},
/*index 4 dpc update setting2*/
{
    .reg_setting =
    {
        {.reg_addr = 0x03, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x04, .reg_data = 0x80, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x95, .reg_data = 0x0a, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x96, .reg_data = 0x30, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x97, .reg_data = 0x0a, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x98, .reg_data = 0x32, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x99, .reg_data = 0x07, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x9a, .reg_data = 0xa9, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf3, .reg_data = 0x80, .delay = 10000, .data_mask = 0x0},
		{.reg_addr = 0xbe, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x09, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x01, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x80, .reg_data = 0x02, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x02, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfe, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xfa, .reg_data = 0x00, .delay = 0, .data_mask = 0x0},
    },
    .size = 17,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/*index 5 reg update page 8*/
{
    .reg_setting =
    {
		{.reg_addr = 0xe0, .reg_data = 0x08, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0xf0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf3, .reg_data = 0x10, .delay = 0, .data_mask = 0x0},
    },
    .size = 3,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/*index 6 reg update page 9*/
{
    .reg_setting =
    {
		{.reg_addr = 0xe0, .reg_data = 0x09, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0x67, .reg_data = 0xf0, .delay = 0, .data_mask = 0x0},
		{.reg_addr = 0xf3, .reg_data = 0x10, .delay = 0, .data_mask = 0x0},
    },
    .size = 3,
    .addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
    .delay = 1,
},
/******************** GC5035_OTP_EDIT_END*******************/
