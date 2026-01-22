#!../../bin/linux-x86_64/lakeshore346

< envPaths

epicsEnvSet("STREAM_PROTOCOL_PATH", "$(TOP)/protocol/")
epicsEnvSet("PORT", "L0")

epicsEnvSet("SCAN", "5")
epicsEnvSet("TEMPSCAN", "1")

dbLoadDatabase("$(TOP)/dbd/lakeshore346.dbd")
lakeshore346_registerRecordDeviceDriver(pdbbase)

drvAsynIPPortConfigure("$(PORT)","localhost:7777")
asynOctetSetInputEos("$(PORT)", 0, "\r\n")
asynOctetSetOutputEos("$(PORT)", 0, "\r\n")

asynSetTraceIOMask("L0", -1, 0x2)
asynSetTraceMask("L0", -1, 0x9)

#lakeshore346_autoload_db("$(TOP)/db","$(PORT)",$(ADDR=0),"P=LS346:,R=,PORT=$(PORT),ADDR=0,SCAN=$(SCAN),TEMPSCAN=$(TEMPSCAN)")
dbLoadRecords("$(TOP)/db/lakeshore346_core.db", "P=LS346:,R=,PORT=$(PORT),ADDR=0,SCAN=$(SCAN),TEMPSCAN=$(TEMPSCAN)")


iocInit()
