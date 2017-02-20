#!/bin/sh
# ****************************************************************
# Read out N single-channel events, 2K samples
# ****************************************************************
#
# caen_daq N_TRIG CH_MASK N_PAIRS
# caen_daq 1000 FF
# for((i=744;i<=750;i++)); do echo -n '----- run $i ----- '; date; ssh vme caen_daq 10000 07 | tee /data/daq/data-$i.dat | crunch /data/mon/prof-$i.root; chmod a-r /data/daq/data-$i.dat; sleep 3100; done
#
#
# v0  initial version			2008-05-30
# v1  added different sample sizes	2008-05-31
# v2   identical
# v3					2008-06-21
#
# 2008-06-16 increased prescale to 4998
#  reduced # of points to 1950 for runs 731-743
#  then back to orig config
#
# 2008-06-21, run 966-
#  investigated prescale, custom size, post trigger
#  increased data rate by 4x, can't keep up with 8x
#  stretched window out to 49.9875 ms
#
# v4  added DAC + sleep 2		2008-07-16
# v5  changed DAC			2008-07-18
# v6  changed DAC, read io bits		2008-07-24
# v7  high-speed daq			2008-07-26
# v8  high-speed, date header		2008-07-29
# v9  no ai
# v10 (from v7) NIM trig, changed DAC	2008-11-01
# v11 removed DAC offsets		2008-11-04
# v12 comments added by Chad		2008-12-07
# v13 run in 100MHz mode		2008-11-22
# v14 based on v12, N_SAMP option	2009-05-31
# v16 based on v14, raised offset	2009-07-16
# At the end of this script is a FAQ explaining some of the details of how it works.

BASE=3210

N_TRIG=$1
echo -n "$N_TRIG triggers " >/dev/stderr

CH_MASK=$2 #second positional parameter (Second one you enter when you run the program)
#Each bit in $CH_MASK refers to a channel
#that bit = 0 means disabled, 1 means enabled

let N_CH="((($CH_MASK & 0x80)>0)+(($CH_MASK&0x40)>0)+(($CH_MASK&0x20)>0)+(($CH_MASK&0x10)>0)+(($CH_MASK&0x8)>0)+(($CH_MASK&0x4)>0)+(($CH_MASK&0x2)>0)+(($CH_MASK&0x1)>0))"
echo -n "$N_CH channels " >/dev/stderr

N_PAIRS=$3
if [[ -z $N_PAIRS ]]; then N_PAIRS=3999; fi

# calculate number of buffers = 2^BUF ~ 256k/N_PAIRS
for ((BUF=10,NS=256; BUF>1; --BUF,NS*=2)); do 
    if ((NS>N_PAIRS)); then break; fi;
done;

# calculate downsample (clock cycles skipped between pairs taken)
# 50ms is the event time, assumes one pair not sampled while waiting for trigger
(( DOWNSAMPLE=(100000*50/(N_PAIRS+1))-1 ))

# number of post trigger events 22 before + 2 latency pairs
(( POSTTRIG=N_PAIRS-24 ))

echo "N_PAIRS=$N_PAIRS BUF=$BUF DOWNSAMPLE=$DOWNSAMPLE POSTTRIG=$POSTTRIG" >/dev/stderr

function WRITE_REGISTER() {
    vme_poke -a VME_A32UD -d VME_D32 -A 0x$BASE$1 $2
#    READ_REGISTER $1 $2
}
function READ_REGISTER() {
    echo -n "address: $1 expect: $2  read: "
    vme_peek -a VME_A32UD -d VME_D32 -e 1 -A 0x$BASE$1
}

###################
# configure the DAQ
###################

WRITE_REGISTER EF24 0		# Reset the board
WRITE_REGISTER EF1C 1		# BLT Event Number (1 event at a time)
WRITE_REGISTER EF00 10		# VME Control Register, Enable BERR 
WRITE_REGISTER 811C 1		# Front Panel I/O Control: TTL
WRITE_REGISTER 810C C0000000	# Trigger Source Enable Mask: Software + External
WRITE_REGISTER 8110 C0000000	# Front Panel Trigger Out: Software + External
WRITE_REGISTER 8120 `printf %x $CH_MASK`	# Channel Enable Mask
WRITE_REGISTER 800C `printf %x $BUF`		# Buffer Organization (Num Buffers = 2^N)
WRITE_REGISTER 8128 `printf %x $DOWNSAMPLE`	# Sample prescale factor
		# note: 100MHz/4000=25kHz(pairs)=50kpts/s
WRITE_REGISTER 8020 `printf %x $N_PAIRS`	# Custom Size (Number of memory locations)
WRITE_REGISTER 8114 `printf %x $POSTTRIG`	# Post Trigger
		# (=3975 + latency=2 + pre=22 = 3999)*2
 
# Channel DAC
WRITE_REGISTER 1098 4000	# raise ped to 1/4 from top (lower window)
WRITE_REGISTER 1198 4000        # raise ped to 1/4 from top (lower window)
WRITE_REGISTER 1298 4000        # raise ped to 1/4 from top (lower window)
#WRITE_REGISTER 1198 C000	# lower ped to 1/8 from bottom (raise window)
# WRITE_REGISTER 1198 E000	# lower ped to 1/8 from bottom (raise window)
# WRITE_REGISTER 1298 2000	# lower ped to 1/8 from bottom (raise window)
#WRITE_REGISTER 1298 2000	# lower ped to 1/8 from bottom (raise window)
#WRITE_REGISTER 1398 e000	# lower ped to 1/8 from bottom (raise window) 
sleep 2

# Channel Configuration Register
# WRITE_REGISTER 8000 18  	# test waveform
WRITE_REGISTER 8000 10  	# external data

# Acquisition Control
WRITE_REGISTER 8100 04  	# start DAQ
#WRITE_REGISTER 8100 14  	# start DAQ in DownSampling mode


###################
# read out the data
###################

for ((trig=0; trig<$N_TRIG; )); do

#address 812c contains the number of events currently stored in the output buffer
#so while there is nothing there, do true, i.e. do nothing. 
    while (( (buf=0x`vme_peek -a VME_A32UD -d VME_D32 -A 0x${BASE}812C`)==0 )); do true; done
    if ((buf>N_TRIG-trig)); then ((buf=N_TRIG-trig)); fi

    if ((buf>1)); then 
	echo -n $buf"." >/dev/stderr
    else #if (((trig%100)==0)); then
	echo -n "." >/dev/stderr
    #fi
    fi

    for ((ibuf=0; ibuf<buf; ++ibuf,++trig)); do
	
	vme_peek -a VME_A32UD -d VME_D32 -e 4 -A 0x${BASE}0000 -b
#-e is number of elements to transfer
#-A is address at which to begin transfer 

	for ((ich=0; ich<N_CH; ++ich)); do		# same as CUSTOM_SIZE
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	# 2*1024 pts
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x3E7 -A 0x${BASE}0000 -b	# 2* 999 pts
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x7CF -A 0x${BASE}0000 -b	# 2*1999 broken!
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	# 2*1999 pts
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x3CF -A 0x${BASE}0000 -b	#  "
	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	# 2*3999 pts
	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x39F -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	# 2*7998 pts
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b	#  "
# 	    vme_peek -a VME_A32UMB -d VME_D32 -e 0x33F -A 0x${BASE}0000 -b	#  "
	done

	vme_peek -a VME_A32UD -d VME_D32 -e 1 -A 0x${BASE}0000 -b > /dev/null
	vme_peek -a VME_A32UD -d VME_D32 -e 1 -A 0x${BASE}8118 -b

    done

done;

echo >/dev/stderr

###################
# stop the DAQ
###################

# Stop acquistion
WRITE_REGISTER 8100 14		# start DAQ in DownSampling mode

# Reset the board
WRITE_REGISTER EF24 0



##############################################################################
##############################################################################
#---
#FAQ for the caen_daq program and the C.A.E.N. ADC
# Please correct and add to it as necessary
# If there's something that you previously did not understand but now
# understand, then please explain it here. 
#---
#----------------------------------------------------------------------------
#Where can I get more information?
#
#The manual for the C.A.E.N. ADC is called V1724_REV12.pdf
#Ours is C.A.E.N. model V1724
#
#----------------------------------------------------------------------------
#What is WRITE_REGISTER?
#
#WRITE_REGISTER() is a function used to send data across the VME bus, from the
#VME computer to the ADC.  It is used to configure the ADC and send commands
#from the VME computer.
#
#WRITE_REGISTER is defined within the shell script caen_daq.  To send data
#across the bus, you need to specify an address to send to, the number of bits
#to send and then finally the data itself.
#
#-a specifies the address mode (including what address space to use)
#-d specifies the number of bits sent.  VME_D32 means 32 bits
#-A specifies the address (an argument of the function WRITE_REGISTER)
#
#The other argument of the WRITE_REGISTER function is the data that is being
#sent.
#
#NB the numerical arguments must be entered in hex as can be seen by looking
#at the function WRITE_REGISTER itself.  However, when calling the function,
#do not use the 0x prefix normally used to specify hex format.
#
#To find out about what all of the addresses refer to and what the values can
#specify, see the file V1724_REV12.pdf
#
#----------------------------------------------------------------------------
# What is BLT?
#
# a) It's safe to ignore what BLT is since we don't use it.
# b) Bacon, Lettuce and Tomato sandwich.
#
#----------------------------------------------------------------------------
# What is the definition of an event?
#
# An event is defined as the data corresponding to one trigger.  The trigger
# comes in the form of a T0 pulse from the accelerator.  For a given event,
# many ADC readouts are carried out.
#
#----------------------------------------------------------------------------
# What is vme_peek and how is data moved from the ADC to the VME computer?
#
# Data is moved from the ADC to the VME computer by executing the shell
# command vme_peek on the VME computer.
#
# You can record several events in succession and store them in the ADC's
# memory before you read any of them out using vme_peek.  The events wait in a
# FIFO buffer until they are read out using vme_peek.  When an event is read
# it is removed from the ADC's memory.  So the ADC continues filling up the
# buffers at the back of the queue while we use vme_peek to read out the old
# ones from the front of the queue.
#
# For info on how to use vme_peek do man vme_peek on the vme computer.  The
# source code is also available on the vme computer.  Not all of the info is
# available in the man page, so don't be surprised if you wind up looking at
# the source code.
#
#----------------------------------------------------------------------------
# What is a sample?  What is downsampling?
#
# After an event is triggered, you take a number of samples in succession.
# Samples are just readouts by the ADC.  Each sample is 14 bits in size.  The
# ADC has a total of 8 channels.
#
# The samples are taken at a maximum rate of once every 10 ns, i.e. at 100 MHz
# which is the clock rate. However, it is possible to slow down the sample
# rate by specifying how many clock cycles should pass inbetween samples.
#
# See "downsampling" in Wikipedia.
#
#----------------------------------------------------------------------------
# What is a point?
#
# Points occur in pairs. A point refers to one of the two samples that are
# taken virtually simultaneously.  They are read out by the ADC in the same
# clock cycle and then stored together in the ADC's memory.
#
# Since each sample is 14 bits in size, there are 28 bits of information
# stored in each pair of points. A pair of points is transfered to the VME
# computer using vme_peek as a 32-bit integer.
#
#----------------------------------------------------------------------------
# What does the following line
#     WRITE_REGISTER 8128 4E1  # 1249  2*3999 pts, 2*80kHz 12.5us pairs
# mean?
#
# Here we're writing to register at address 0x8128 and giving it the value
# 0x4E1.  0x4E1 is 1249 when converted to decimal.  Register 0x8128 records
# the downsample factor.  In other words it is the number of clock cycles to
# wait before taking the next pair of points. As stated in the manual,
# register 0x8128 allows to set N: sampling frequency will be divided by N+1.
#
# Therefore we get: 100,000,000 Hz/1250 = 80,000 Hz So we read data at a rate
# of one pair per 12.5 us.  If T0's come at a rate of 20 Hz, then this means
# we get: 80,000 Hz/20 Hz = 3999+1 pairs of points.  We leave out the last
# point in order to not miss the trigger for the next event.
#
#---------------------------------------------------------------------------
# What does the line
#    WRITE_REGISTER 800C 6       
# mean and how does it translate into 64 buffers/ch, 8192 data pts?
#
# Remember you can take several events in succession before reading out any of
# them.  For this reason, we divide the memory of the ADC up into event
# buffers, each of which corresponds to one event. So in this case, the term
# event buffer just refers to a subdivision of memory that is devoted to
# storing one event's worth of data.  The DAQ continues filling up event
# buffers while you read out and free old event buffers.
#
# Each channel has a certain amount of SRAM memory.  This memory can be
# divided into event buffers of programmable size (see p 8 of
# V1724_REV12.pdf).
#
# According to what's written on the front, our ADC is a C.A.E.N. model V1724.
# This is a bit confusing since according to the manual V1724 is both a model
# number for which there are several versions, one of which has the same
# version number as the model number (see p 8 of V1724_REV12.pdf).  In the end
# though we have an ADC with 512 Ksamples / channel of SRAM memory.
#
# The amount of memory available per channel fixes the value of (event buffer
# size)*(number of events). In our case, we have divided the 512*1024 samples
# worth of memory into 64 event buffers. Therefore, we can store a total of
# 8192 samples in each event buffer.
#
#---------------------------------------------------------------------------
# In the commands :
#
#            vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b        # 2*3999 pts
#            vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b        #  "
#            vme_peek -a VME_A32UMB -d VME_D32 -e 0x400 -A 0x${BASE}0000 -b        #  "
#            vme_peek -a VME_A32UMB -d VME_D32 -e 0x39F -A 0x${BASE}0000 -b        #  "
#
# why are you limited to reading 0x400 points at a time?
#
# This is because the events are available for reading on the event readout
# buffer which, as given on p. 49 of V1724_REV12.pdf, is located between
# addresses 0x0000-0x0FFC.  We read out in units of 1 word=4 bytes and the
# addresses are indexed by byte.  So we are limited to reading out
# 0x0FFC/4=0x0400 words at a time.
#
# The rest of the data that aren't available for reading must be stored
# internally before being pushed out to the readout buffer.
#
#---------------------------------------------------------------------------
##############################################################################
##############################################################################
