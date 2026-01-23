# SPDX-FileCopyrightText: 2003 Argonne National Laboratory
#
# SPDX-License-Identifier: EPICS

TOP=../..

include $(TOP)/configure/CONFIG

PROD_IOC = lakeshore346
DBD += $(PROD_IOC).dbd

$(PROD_IOC)_DBD += base.dbd
$(PROD_IOC)_DBD += asyn.dbd
$(PROD_IOC)_DBD += drvAsynIPPort.dbd
$(PROD_IOC)_DBD += calc.dbd
$(PROD_IOC)_DBD += stream.dbd
$(PROD_IOC)_DBD += busySupport.dbd
$(PROD_IOC)_DBD += asSupport.dbd
$(PROD_IOC)_DBD += $(PROD_IOC)_asub_sup.dbd
$(PROD_IOC)_DBD += $(PROD_IOC)_autoload_db.dbd

$(PROD_IOC)_SRCS += $(PROD_IOC)_registerRecordDeviceDriver.cpp
$(PROD_IOC)_SRCS += $(PROD_IOC)Main.cpp
$(PROD_IOC)_SRCS += $(PROD_IOC)_util.cpp
$(PROD_IOC)_SRCS += $(PROD_IOC)_asub_sup.cpp
$(PROD_IOC)_SRCS += $(PROD_IOC)_autoload_db.cpp

$(PROD_IOC)_LIBS += asyn
$(PROD_IOC)_LIBS += calc
$(PROD_IOC)_LIBS += stream
$(PROD_IOC)_LIBS += busy
$(PROD_IOC)_LIBS += autosave
$(PROD_IOC)_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
$(PROD_IOC)_LIBS += $(EPICS_BASE_IOC_LIBS)

USR_CPPFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET

include $(TOP)/configure/RULES
