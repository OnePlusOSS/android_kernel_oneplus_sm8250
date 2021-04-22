#obj-$(CONFIG_SENSOR_HALL_MXM1120)	+=hall_ic/
#obj-$(CONFIG_SENSOR_HALL_IST8801)	+=ist_hall_ic/
#obj-$(CONFIG_TRI_STATE_KEY)     += tri_state_key.o
#obj-$(CONFIG_HALL_TRI_STATE_KEY)		+= oneplus_tri_key.o
ifeq ($(CONFIG_HALL_TRI_STATE_KEY),y)
obj-y +=hall_ic/
obj-y +=ist_hall_ic/
obj-y +=oneplus_tri_key.o
else
obj-y +=tri_state_key.o
endif
