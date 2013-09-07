// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceUmd.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"

const u64 MICRO_DELAY_ACTIVATE = 4000;

static u8 umdActivated = 1;
static u32 umdStatus = 0;
static u32 umdErrorStat = 0;
static int driveCBId = -1;
static int umdStatTimeoutEvent = -1;
static int umdStatChangeEvent = -1;
static std::vector<SceUID> umdWaitingThreads;
static std::map<SceUID, u64> umdPausedWaitTimeouts;

struct PspUmdInfo {
	u32_le size;
	u32_le type;
};

void __UmdStatTimeout(u64 userdata, int cyclesLate);
void __UmdStatChange(u64 userdata, int cyclesLate);
void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId);
void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId);

void __UmdInit()
{
	umdStatTimeoutEvent = CoreTiming::RegisterEvent("UmdTimeout", __UmdStatTimeout);
	umdStatChangeEvent = CoreTiming::RegisterEvent("UmdChange", __UmdStatChange);
	umdActivated = 1;
	umdStatus = 0;
	umdErrorStat = 0;
	driveCBId = -1;

	__KernelRegisterWaitTypeFuncs(WAITTYPE_UMD, __UmdBeginCallback, __UmdEndCallback);
}

void __UmdDoState(PointerWrap &p)
{
	p.Do(umdActivated);
	p.Do(umdStatus);
	p.Do(umdErrorStat);
	p.Do(driveCBId);
	p.Do(umdStatTimeoutEvent);
	CoreTiming::RestoreRegisterEvent(umdStatTimeoutEvent, "UmdTimeout", __UmdStatTimeout);
	p.Do(umdStatChangeEvent);
	CoreTiming::RestoreRegisterEvent(umdStatChangeEvent, "UmdChange", __UmdStatChange);
	p.Do(umdWaitingThreads);
	p.Do(umdPausedWaitTimeouts);
	p.DoMarker("sceUmd");
}

u8 __KernelUmdGetState()
{
	u8 state = PSP_UMD_PRESENT;
	if (umdActivated) {
		state |= PSP_UMD_READY;
		state |= PSP_UMD_READABLE;
	}
	// TODO: My tests give PSP_UMD_READY but I suppose that's when it's been sitting in the drive?
	else
		state |= PSP_UMD_NOT_READY;
	return state;
}

void __UmdStatChange(u64 userdata, int cyclesLate)
{
	// TODO: Why not a bool anyway?
	umdActivated = userdata & 0xFF;

	// Wake anyone waiting on this.
	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		SceUID threadID = umdWaitingThreads[i];

		u32 error;
		SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
		u32 stat = __KernelGetWaitValue(threadID, error);
		bool keep = false;
		if (waitID == 1) {
			if ((stat & __KernelUmdGetState()) != 0)
				__KernelResumeThreadFromWait(threadID, 0);
			// Only if they are still waiting do we keep them in the list.
			else
				keep = true;
		}

		if (!keep)
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
	}
}

void __KernelUmdActivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READABLE;
	__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);

	// Don't activate immediately, take time to "spin up."
	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	CoreTiming::ScheduleEvent(usToCycles(MICRO_DELAY_ACTIVATE), umdStatChangeEvent, 1);
}

void __KernelUmdDeactivate()
{
	u32 notifyArg = PSP_UMD_PRESENT | PSP_UMD_READY;
	__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);

	CoreTiming::RemoveAllEvents(umdStatChangeEvent);
	__UmdStatChange(0, 0);
}

void __UmdBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
	if (waitID == 1)
	{
		// This means two callbacks in a row.  PSP crashes if the same callback runs inside itself.
		// TODO: Handle this better?
		if (umdPausedWaitTimeouts.find(pauseKey) != umdPausedWaitTimeouts.end())
			return;

		_dbg_assert_msg_(HLE, umdStatTimeoutEvent != -1, "Must have a umd timer");
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(umdStatTimeoutEvent, threadID);
		if (cyclesLeft != 0)
			umdPausedWaitTimeouts[pauseKey] = CoreTiming::GetTicks() + cyclesLeft;
		else
			umdPausedWaitTimeouts[pauseKey] = 0;

		for (auto it = umdWaitingThreads.begin(); it < umdWaitingThreads.end(); ++it)
		{
			if (*it == threadID)
				umdWaitingThreads.erase(it--);
		}

		DEBUG_LOG(HLE, "sceUmdWaitDriveStatCB: Suspending lock wait for callback");
	}
	else
		WARN_LOG_REPORT(HLE, "sceUmdWaitDriveStatCB: beginning callback with bad wait id?");
}

void __UmdEndCallback(SceUID threadID, SceUID prevCallbackId)
{
	SceUID pauseKey = prevCallbackId == 0 ? threadID : prevCallbackId;

	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
	u32 stat = __KernelGetWaitValue(threadID, error);
	if (umdPausedWaitTimeouts.find(pauseKey) == umdPausedWaitTimeouts.end())
	{
		WARN_LOG_REPORT(HLE, "__UmdEndCallback(): UMD paused wait missing");

		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	u64 waitDeadline = umdPausedWaitTimeouts[pauseKey];
	umdPausedWaitTimeouts.erase(pauseKey);

	// TODO: Don't wake up if __KernelCurHasReadyCallbacks()?

	if ((stat & __KernelUmdGetState()) != 0)
	{
		__KernelResumeThreadFromWait(threadID, 0);
		return;
	}

	s64 cyclesLeft = waitDeadline - CoreTiming::GetTicks();
	if (cyclesLeft < 0 && waitDeadline != 0)
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	else
	{
		_dbg_assert_msg_(HLE, umdStatTimeoutEvent != -1, "Must have a umd timer");
		CoreTiming::ScheduleEvent(cyclesLeft, umdStatTimeoutEvent, __KernelGetCurThread());

		umdWaitingThreads.push_back(threadID);

		DEBUG_LOG(HLE, "sceUmdWaitDriveStatCB: Resuming lock wait for callback");
	}
}

int sceUmdCheckMedium()
{
	DEBUG_LOG(HLE, "1=sceUmdCheckMedium()");
	return 1; //non-zero: disc in drive
}
	
u32 sceUmdGetDiscInfo(u32 infoAddr)
{
	DEBUG_LOG(HLE, "sceUmdGetDiscInfo(%08x)", infoAddr);

	if (Memory::IsValidAddress(infoAddr)) {
		PSPPointer<PspUmdInfo> info;
		info = infoAddr;
		if (info->size != 8)
			return PSP_ERROR_UMD_INVALID_PARAM;

		info->type = PSP_UMD_TYPE_GAME;
		return 0;
	} else
		return PSP_ERROR_UMD_INVALID_PARAM;
}

int sceUmdActivate(u32 mode, const char *name)
{
	if (mode < 1 || mode > 2)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdActivate();

	if (mode == 1) {
		DEBUG_LOG(HLE, "0=sceUmdActivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(HLE, "UNTESTED 0=sceUmdActivate(%d, %s)", mode, name);
	}

	return 0;
}

int sceUmdDeactivate(u32 mode, const char *name)
{
	// Why 18?  No idea.
	if (mode > 18)
		return PSP_ERROR_UMD_INVALID_PARAM;

	__KernelUmdDeactivate();

	if (mode == 1) {
		DEBUG_LOG(HLE, "0=sceUmdDeactivate(%d, %s)", mode, name);
	} else {
		ERROR_LOG(HLE, "UNTESTED 0=sceUmdDeactivate(%d, %s)", mode, name);
	}

	return 0;
}

u32 sceUmdRegisterUMDCallBack(u32 cbId)
{
	int retVal;

	// TODO: If the callback is invalid, return PSP_ERROR_UMD_INVALID_PARAM.
	if (cbId == 0)
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	else {
		// Remove the old one, we're replacing.
		if (driveCBId != -1)
			__KernelUnregisterCallback(THREAD_CALLBACK_UMD, driveCBId);

		retVal = __KernelRegisterCallback(THREAD_CALLBACK_UMD, cbId);
		driveCBId = cbId;
	}

	DEBUG_LOG(HLE, "%d=sceUmdRegisterUMDCallback(id=%08x)", retVal, cbId);
	return retVal;
}

int sceUmdUnRegisterUMDCallBack(int cbId)
{
	int retVal;

	if (cbId != driveCBId)
		retVal = PSP_ERROR_UMD_INVALID_PARAM;
	else {
		retVal = cbId;
		driveCBId = -1;
		__KernelUnregisterCallback(THREAD_CALLBACK_UMD, cbId);
	}

	DEBUG_LOG(HLE, "%08x=sceUmdUnRegisterUMDCallBack(id=%08x)", retVal, cbId);
	return retVal;
}

u32 sceUmdGetDriveStat()
{
	//u32 retVal = PSP_UMD_INITED | PSP_UMD_READY | PSP_UMD_PRESENT;
	u32 retVal = __KernelUmdGetState();
	DEBUG_LOG(HLE,"0x%02x=sceUmdGetDriveStat()", retVal);
	return retVal;
}

void __UmdStatTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_UMD, error);
	// Assuming it's still waiting.
	if (waitID == 1)
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);

	for (size_t i = 0; i < umdWaitingThreads.size(); ++i) {
		if (umdWaitingThreads[i] == threadID)
			umdWaitingThreads.erase(umdWaitingThreads.begin() + i--);
	}
}

void __UmdWaitStat(u32 timeout)
{
	// This happens to be how the hardware seems to time things.
	if (timeout <= 4)
		timeout = 15;
	else if (timeout <= 215)
		timeout = 250;

	CoreTiming::ScheduleEvent(usToCycles((int) timeout), umdStatTimeoutEvent, __KernelGetCurThread());
}

/** 
* Wait for a drive to reach a certain state
*
* @param stat - The drive stat to wait for.
* @return < 0 on error
*
*/
int sceUmdWaitDriveStat(u32 stat)
{
	if (stat == 0) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStat(stat = %08x): bad status", stat);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStat(stat = %08x): dispatch disabled", stat);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStat(stat = %08x): inside interrupt", stat);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStat(stat = %08x): waiting", stat);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited");
		return 0;
	}

	DEBUG_LOG(HLE, "0=sceUmdWaitDriveStat(stat = %08x)", stat);
	return 0;
}

int sceUmdWaitDriveStatWithTimer(u32 stat, u32 timeout)
{
	if (stat == 0) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): bad status", stat, timeout);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): dispatch disabled", stat, timeout);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): inside interrupt", stat, timeout);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d): waiting", stat, timeout);
		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, 0, "umd stat waited with timer");
		return 0;
	} else {
		hleReSchedule("umd stat checked");
	}

	DEBUG_LOG(HLE, "0=sceUmdWaitDriveStatWithTimer(stat = %08x, timeout = %d)", stat, timeout);
	return 0;
}

int sceUmdWaitDriveStatCB(u32 stat, u32 timeout)
{
	if (stat == 0) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): bad status", stat, timeout);
		return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
	}

	if (!__KernelIsDispatchEnabled()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): dispatch disabled", stat, timeout);
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}
	if (__IsInInterrupt()) {
		DEBUG_LOG(HLE, "sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): inside interrupt", stat, timeout);
		return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	}

	hleCheckCurrentCallbacks();
	if ((stat & __KernelUmdGetState()) == 0) {
		DEBUG_LOG(HLE, "0=sceUmdWaitDriveStatCB(stat = %08x, timeout = %d): waiting", stat, timeout);
		if (timeout == 0) {
			timeout = 8000;
		}

		__UmdWaitStat(timeout);
		umdWaitingThreads.push_back(__KernelGetCurThread());
		__KernelWaitCurThread(WAITTYPE_UMD, 1, stat, 0, true, "umd stat waited");
	} else {
		hleReSchedule("umd stat waited");
	}

	DEBUG_LOG(HLE, "0=sceUmdWaitDriveStatCB(stat = %08x, timeout = %d)", stat, timeout);
	return 0;
}

u32 sceUmdCancelWaitDriveStat()
{
	DEBUG_LOG(HLE, "0=sceUmdCancelWaitDriveStat()");

	__KernelTriggerWait(WAITTYPE_UMD, 1, SCE_KERNEL_ERROR_WAIT_CANCEL, "umd stat ready", true);
	// TODO: We should call UnscheduleEvent() event here?
	// But it's not often used anyway, and worst-case it will just do nothing unless it waits again.
	return 0;
}

u32 sceUmdGetErrorStat()
{
	DEBUG_LOG(HLE,"%i=sceUmdGetErrorStat()", umdErrorStat);
	return umdErrorStat;
}

u32 sceUmdReplaceProhibit()
{
	DEBUG_LOG(HLE,"sceUmdReplaceProhibit()");
	return 0;
}

u32 sceUmdReplacePermit()
{
	DEBUG_LOG(HLE,"sceUmdReplacePermit()");
	return 0;
}

const HLEFunction sceUmdUser[] = 
{
	{0xC6183D47,WrapI_UC<sceUmdActivate>,"sceUmdActivate"},
	{0x6B4A146C,&WrapU_V<sceUmdGetDriveStat>,"sceUmdGetDriveStat"},
	{0x46EBB729,WrapI_V<sceUmdCheckMedium>,"sceUmdCheckMedium"},
	{0xE83742BA,WrapI_UC<sceUmdDeactivate>,"sceUmdDeactivate"},
	{0x8EF08FCE,WrapI_U<sceUmdWaitDriveStat>,"sceUmdWaitDriveStat"},
	{0x56202973,WrapI_UU<sceUmdWaitDriveStatWithTimer>,"sceUmdWaitDriveStatWithTimer"},
	{0x4A9E5E29,WrapI_UU<sceUmdWaitDriveStatCB>,"sceUmdWaitDriveStatCB"},
	{0x6af9b50a,WrapU_V<sceUmdCancelWaitDriveStat>,"sceUmdCancelWaitDriveStat"},
	{0x20628E6F,&WrapU_V<sceUmdGetErrorStat>,"sceUmdGetErrorStat"},
	{0x340B7686,WrapU_U<sceUmdGetDiscInfo>,"sceUmdGetDiscInfo"},
	{0xAEE7404D,&WrapU_U<sceUmdRegisterUMDCallBack>,"sceUmdRegisterUMDCallBack"},
	{0xBD2BDE07,&WrapI_I<sceUmdUnRegisterUMDCallBack>,"sceUmdUnRegisterUMDCallBack"},
	{0x87533940,WrapU_V<sceUmdReplaceProhibit>,"sceUmdReplaceProhibit"},
	{0xCBE9F02A,WrapU_V<sceUmdReplacePermit>,"sceUmdReplacePermit"},
	{0x14c6c45c,0,"sceUmdUser_14C6C45C"},
	{0xb103fa38,0,"sceUmdUser_B103FA38"},
};

void Register_sceUmdUser()
{
	RegisterModule("sceUmdUser", ARRAY_SIZE(sceUmdUser), sceUmdUser);
}
