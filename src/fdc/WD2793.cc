// $Id$

#include "WD2793.hh"
#include "DiskDrive.hh"
#include "CliComm.hh"
#include "Clock.hh"
#include "MSXException.hh"
#include "serialize.hh"
#include "unreachable.hh"

namespace openmsx {

// Status register
static const int BUSY             = 0x01;
static const int INDEX            = 0x02;
static const int S_DRQ            = 0x02;
static const int TRACK00          = 0x04;
static const int LOST_DATA        = 0x04;
static const int CRC_ERROR        = 0x08;
static const int SEEK_ERROR       = 0x10;
static const int RECORD_NOT_FOUND = 0x10;
static const int HEAD_LOADED      = 0x20;
static const int RECORD_TYPE      = 0x20;
static const int WRITE_PROTECTED  = 0x40;
static const int NOT_READY        = 0x80;

// Command register
static const int STEP_SPEED = 0x03;
static const int V_FLAG     = 0x04;
static const int E_FLAG     = 0x04;
static const int H_FLAG     = 0x08;
static const int T_FLAG     = 0x10;
static const int M_FLAG     = 0x10;
static const int N2R_IRQ    = 0x01;
static const int R2N_IRQ    = 0x02;
static const int IDX_IRQ    = 0x04;
static const int IMM_IRQ    = 0x08;

// Sync point types
enum SyncPointType { SCHED_FSM, SCHED_IDX_IRQ };


WD2793::WD2793(Scheduler& scheduler, DiskDrive& drive_, CliComm& cliComm_,
               EmuTime::param time)
	: Schedulable(scheduler)
	, drive(drive_)
	, cliComm(cliComm_)
	, drqTime(EmuTime::infinity)
{
	// avoid (harmless) UMR in serialize()
	dataCurrent = 0;
	dataAvailable = 0;
	lastWasA1 = false;
	setDrqRate();

	reset(time);
}

void WD2793::reset(EmuTime::param time)
{
	removeSyncPoint(SCHED_FSM);
	removeSyncPoint(SCHED_IDX_IRQ);
	fsmState = FSM_NONE;

	statusReg = 0;
	trackReg = 0;
	dataReg = 0;
	directionIn = true;

	drqTime.reset(EmuTime::infinity); // DRQ = false
	resetIRQ();
	immediateIRQ = false;

	// Execute Restore command
	sectorReg = 0x01;
	setCommandReg(0x03, time);
}

bool WD2793::getDTRQ(EmuTime::param time)
{
	return time >= drqTime.getTime();
}

bool WD2793::peekDTRQ(EmuTime::param time)
{
	return getDTRQ(time);
}

void WD2793::setDrqRate()
{
	drqTime.setFreq(trackData.getLength() * DiskDrive::ROTATIONS_PER_SECOND);
}

bool WD2793::getIRQ(EmuTime::param /*time*/)
{
	//PRT_DEBUG("WD2793::getIRQ() " << INTRQ);
	return INTRQ || immediateIRQ;
}

bool WD2793::peekIRQ(EmuTime::param time)
{
	return getIRQ(time);
}

void WD2793::setIRQ()
{
	INTRQ = true;
}

void WD2793::resetIRQ()
{
	INTRQ = false;
}

void WD2793::setCommandReg(byte value, EmuTime::param time)
{
	//PRT_DEBUG("WD2793::setCommandReg() 0x" << std::hex << (int)value);
	removeSyncPoint(SCHED_FSM);

	commandReg = value;
	resetIRQ();
	switch (commandReg & 0xF0) {
		case 0x00: // restore
		case 0x10: // seek
		case 0x20: // step
		case 0x30: // step (Update trackRegister)
		case 0x40: // step-in
		case 0x50: // step-in (Update trackRegister)
		case 0x60: // step-out
		case 0x70: // step-out (Update trackRegister)
			startType1Cmd(time);
			break;

		case 0x80: // read sector
		case 0x90: // read sector (multi)
		case 0xA0: // write sector
		case 0xB0: // write sector (multi)
			startType2Cmd(time);
			break;

		case 0xC0: // Read Address
		case 0xE0: // read track
		case 0xF0: // write track
			startType3Cmd(time);
			break;

		case 0xD0: // Force interrupt
			startType4Cmd(time);
			break;
	}
}

byte WD2793::getStatusReg(EmuTime::param time)
{
	if (((commandReg & 0x80) == 0) || ((commandReg & 0xF0) == 0xD0)) {
		// Type I or type IV command
		statusReg &= ~(INDEX | TRACK00 | HEAD_LOADED | WRITE_PROTECTED);
		if (drive.indexPulse(time)) {
			statusReg |=  INDEX;
		}
		if (drive.isTrack00()) {
			statusReg |=  TRACK00;
		}
		if (drive.headLoaded(time)) {
			statusReg |=  HEAD_LOADED;
		}
		if (drive.isWriteProtected()) {
			statusReg |=  WRITE_PROTECTED;
		}
	} else {
		// Not type I command so bit 1 should be DRQ
		if (getDTRQ(time)) {
			statusReg |=  S_DRQ;
		} else {
			statusReg &= ~S_DRQ;
		}
	}

	if (drive.isDiskInserted()) {
		statusReg &= ~NOT_READY;
	} else {
		statusReg |=  NOT_READY;
	}

	resetIRQ();
	//PRT_DEBUG("WD2793::getStatusReg() 0x" << std::hex << (int)statusReg);
	return statusReg;
}

byte WD2793::peekStatusReg(EmuTime::param time)
{
	return getStatusReg(time);
}

void WD2793::setTrackReg(byte value, EmuTime::param /*time*/)
{
	//PRT_DEBUG("WD2793::setTrackReg() 0x" << std::hex << (int)value);
	trackReg = value;
}

byte WD2793::getTrackReg(EmuTime::param /*time*/)
{
	return trackReg;
}

byte WD2793::peekTrackReg (EmuTime::param time)
{
	return getTrackReg(time);
}

void WD2793::setSectorReg(byte value, EmuTime::param /*time*/)
{
	//PRT_DEBUG("WD2793::setSectorReg() 0x" << std::hex << (int)value);
	sectorReg = value;
}

byte WD2793::getSectorReg(EmuTime::param /*time*/)
{
	return sectorReg;
}

byte WD2793::peekSectorReg(EmuTime::param time)
{
	return getSectorReg(time);
}

void WD2793::setDataReg(byte value, EmuTime::param time)
{
	//PRT_DEBUG("WD2793::setDataReg() 0x" << std::hex << (int)value);
	dataReg = value;

	if (!getDTRQ(time)) return;
	assert(statusReg & BUSY);

	if (((commandReg & 0xE0) == 0xA0) || // write sector
	    ((commandReg & 0xF0) == 0xF0)) { // write track
		// handle lost bytes
		drqTime += 1; // time when next byte will be accepted
		while (dataAvailable && unlikely(getDTRQ(time))) {
			statusReg |= LOST_DATA;
			drqTime += 1;
			trackData.write(dataCurrent++, 0);
			crc.update(0);
			dataAvailable--;
		}

		byte write = value; // written value not always same as given value
		if ((commandReg & 0xF0) == 0xF0) {
			// write track, handle chars with special meaning
			bool prevA1 = lastWasA1;
			lastWasA1 = false;
			if (value == 0xF5) {
				// write A1 with missing clock transitions
				write = 0xA1;
				lastWasA1 = true;
				// Initialize CRC: the calculated CRC value
				// includes the 3 A1 bytes. So when starting
				// from the initial value 0xffff, we should not
				// re-initialize the CRC value on the 2nd and
				// 3rd A1 byte. Though what we do instead is on
				// each A1 byte initialize the value as if
				// there were already 2 A1 bytes written.
				crc.init<0xA1, 0xA1>();
			} else if (value == 0xF6) {
				// write C2 with missing clock transitions
				write = 0xC2;
			} else if (value == 0xF7) {
				// write 2 CRC bytes, big endian
				word crcVal = crc.getValue();
				if (dataAvailable) {
					drqTime += 1;
					trackData.write(dataCurrent++, crcVal >> 8);
					dataAvailable--;
				}
				write = crcVal & 0xFF;
			} else if (value == 0xFE) {
				// Record locations of 0xA1 (with missing clock
				// transition) followed by 0xFE. The FE byte has
				// no special meaning for the WD2793 itself,
				// but it does for the DMK file format.
				if (prevA1) {
					trackData.addIdam(dataCurrent);
				}
			}
		}
		if (dataAvailable) {
			trackData.write(dataCurrent++, write);
			crc.update(write);
			dataAvailable--;
		}
		assert(!dataAvailable || !getDTRQ(time));
	}
}

byte WD2793::getDataReg(EmuTime::param time)
{
	if ((((commandReg & 0xE0) == 0x80) ||   // read sector
	     ((commandReg & 0xF0) == 0xC0) ||   // read address
	     ((commandReg & 0xF0) == 0xE0)) &&  // read track
	    getDTRQ(time)) {
		assert(statusReg & BUSY);

		dataReg = trackData.read(dataCurrent++);
		crc.update(dataReg);
		dataAvailable--;
		drqTime += 1; // time when the next byte will be available
		while (dataAvailable && unlikely(getDTRQ(time))) {
			statusReg |= LOST_DATA;
			dataReg = trackData.read(dataCurrent++);
			crc.update(dataReg);
			dataAvailable--;
			drqTime += 1;
		}
		assert(!dataAvailable || !getDTRQ(time));
		if (dataAvailable == 0) {
			if ((commandReg & 0xE0) == 0x80) {
				// read sector
				// update crc status flag
				word diskCrc  = 256 * trackData.read(dataCurrent++);
				     diskCrc +=       trackData.read(dataCurrent++);
				if (diskCrc == crc.getValue()) {
					statusReg &= ~CRC_ERROR;
				} else {
					statusReg |=  CRC_ERROR;
				}
				if (!(commandReg & M_FLAG)) {
					endCmd();
				} else {
					// TODO multi sector read
					sectorReg++;
					endCmd();
				}
			} else {
				// read track, read address
				// TODO check CRC error on 'read address'
				endCmd();
			}
		}
	}
	return dataReg;
}

byte WD2793::peekDataReg(EmuTime::param time)
{
	if ((((commandReg & 0xE0) == 0x80) ||   // read sector
	     ((commandReg & 0xF0) == 0xC0) ||   // read address
	     ((commandReg & 0xF0) == 0xE0)) &&  // read track
	    peekDTRQ(time)) {
		return trackData.read(dataCurrent);
	} else {
		return dataReg;
	}
}


void WD2793::schedule(FSMState state, EmuTime::param time)
{
	assert(!pendingSyncPoint(SCHED_FSM));
	fsmState = state;
	setSyncPoint(time, SCHED_FSM);
}

void WD2793::executeUntil(EmuTime::param time, int userData)
{
	if (userData == SCHED_IDX_IRQ) {
		INTRQ = true;
		return;
	}

	assert(userData == SCHED_FSM);
	FSMState state = fsmState;
	fsmState = FSM_NONE;
	switch (state) {
		case FSM_SEEK:
			if ((commandReg & 0x80) == 0x00) {
				// Type I command
				seekNext(time);
			}
			break;
		case FSM_TYPE2_WAIT_LOAD:
			if ((commandReg & 0xC0) == 0x80)  {
				// Type II command
				type2WaitLoad(time);
			}
			break;
		case FSM_TYPE2_LOADED:
			if ((commandReg & 0xC0) == 0x80)  {
				// Type II command
				type2Loaded(time);
			}
			break;
		case FSM_TYPE2_ROTATED:
			if ((commandReg & 0xC0) == 0x80)  {
				// Type II command
				type2Rotated(time);
			}
			break;
		case FSM_WRITE_SECTOR:
			if ((commandReg & 0xE0) == 0xA0) {
				// write sector command
				doneWriteSector();
			}
			break;
		case FSM_TYPE3_WAIT_LOAD:
			if (((commandReg & 0xC0) == 0xC0) &&
			    ((commandReg & 0xF0) != 0xD0)) {
				// Type III command
				type3WaitLoad(time);
			}
			break;
		case FSM_TYPE3_LOADED:
			if (((commandReg & 0xC0) == 0xC0) &&
			    ((commandReg & 0xF0) != 0xD0)) {
				// Type III command
				type3Loaded(time);
			}
			break;
		case FSM_TYPE3_ROTATED:
			if (((commandReg & 0xC0) == 0xC0) &&
			    ((commandReg & 0xF0) != 0xD0)) {
				// Type III command
				type3Rotated(time);
			}
			break;
		case FSM_WRITE_TRACK:
			if ((commandReg & 0xF0) == 0xF0) {
				// write track command
				doneWriteTrack();
			}
			break;
		default:
			UNREACHABLE;
	}
}

void WD2793::startType1Cmd(EmuTime::param time)
{
	statusReg &= ~(SEEK_ERROR | CRC_ERROR);
	statusReg |= BUSY;

	drive.setHeadLoaded((commandReg & H_FLAG) != 0, time);

	switch (commandReg & 0xF0) {
		case 0x00: // restore
			trackReg = 0xFF;
			dataReg  = 0x00;
			seek(time);
			break;

		case 0x10: // seek
			seek(time);
			break;

		case 0x20: // step
		case 0x30: // step (Update trackRegister)
			step(time);
			break;

		case 0x40: // step-in
		case 0x50: // step-in (Update trackRegister)
			directionIn = true;
			step(time);
			break;

		case 0x60: // step-out
		case 0x70: // step-out (Update trackRegister)
			directionIn = false;
			step(time);
			break;
	}
}

void WD2793::seek(EmuTime::param time)
{
	if (trackReg == dataReg) {
		endType1Cmd();
	} else {
		directionIn = (dataReg > trackReg);
		step(time);
	}
}

void WD2793::step(EmuTime::param time)
{
	const int timePerStep[4] = {
		// in ms, in case a 1MHz clock is used (as in MSX)
		6, 12, 20, 30
	};

	if ((commandReg & T_FLAG) || ((commandReg & 0xE0) == 0x00)) {
		// Restore or seek  or  T_FLAG
		if (directionIn) {
			trackReg++;
		} else {
			trackReg--;
		}
	}
	if (!directionIn && drive.isTrack00()) {
		trackReg = 0;
		endType1Cmd();
	} else {
		drive.step(directionIn, time);
		schedule(FSM_SEEK,
		         time + EmuDuration::msec(timePerStep[commandReg & STEP_SPEED]));
	}
}

void WD2793::seekNext(EmuTime::param time)
{
	if ((commandReg & 0xE0) == 0x00) {
		// Restore or seek
		seek(time);
	} else {
		endType1Cmd();
	}
}

void WD2793::endType1Cmd()
{
	if (commandReg & V_FLAG) {
		// verify sequence
		// TODO verify sequence
	}
	endCmd();
}


void WD2793::startType2Cmd(EmuTime::param time)
{
	statusReg &= ~(LOST_DATA   | RECORD_NOT_FOUND |
	               RECORD_TYPE | WRITE_PROTECTED);
	statusReg |= BUSY;

	if (!drive.isDiskInserted()) {
		endCmd();
	} else {
		// WD2795/WD2797 would now set SSO output
		drive.setHeadLoaded(true, time);

		if (commandReg & E_FLAG) {
			schedule(FSM_TYPE2_WAIT_LOAD,
			         time + EmuDuration::msec(30)); // when 1MHz clock
		} else {
			type2WaitLoad(time);
		}
	}
}

void WD2793::type2WaitLoad(EmuTime::param time)
{
	// TODO wait till head loaded, I arbitrarily took 1ms delay
	schedule(FSM_TYPE2_LOADED, time + EmuDuration::msec(1));
}

void WD2793::type2Loaded(EmuTime::param time)
{
	if (((commandReg & 0xE0) == 0xA0) && (drive.isWriteProtected())) {
		// write command and write protected
		PRT_DEBUG("WD2793: write protected");
		statusReg |= WRITE_PROTECTED;
		endCmd();
		return;
	}

	// Locate sector on disk.
	RawTrack::Sector sectorInfo;
	int firstIdx = -1;
	EmuTime next = time;
	while (true) {
		try {
			next = drive.getNextSector(next, trackData, sectorInfo);
			setDrqRate();
		} catch (MSXException& /*e*/) {
			statusReg |= RECORD_NOT_FOUND;
			endCmd();
			return;
		}
		// TODO we should spread this search over time, so that
		// the CRC status bit toggles correctly.
		if ((next == EmuTime::infinity) ||      // no sectors on this track
		    (sectorInfo.addrIdx == firstIdx)) { // already seen this index
			// TODO real WD2793 searches till 5 index holes
			//      have passed
			// TODO actually let EmuTime pass
			statusReg |= RECORD_NOT_FOUND;
			endCmd();
			return;
		}
		if (sectorInfo.addrCrcErr) {
			statusReg |=  CRC_ERROR;
		} else {
			statusReg &= ~CRC_ERROR;
		}
		if (firstIdx == -1) firstIdx = sectorInfo.addrIdx;
		if (sectorInfo.addrCrcErr)          continue;
		if (sectorInfo.track  != trackReg)  continue;
		if (sectorInfo.sector != sectorReg) continue;
		// TODO implement (optional) head compare
		break;
	}

	// Found sector.
	// Get sectorsize from disk: 128, 256, 512 or 1024 bytes
	// Verified on real WD2793:
	//   sizecode=255 results in a sector size of 1024 bytes,
	// This suggests the WD2793 only looks at the lower 2 bits.
	dataAvailable = 128 << (sectorInfo.sizeCode & 3);
	dataCurrent = sectorInfo.dataIdx;

	if ((commandReg & 0xE0) == 0xA0) {
		// write sector
		// TODO actually we should
		//  - skip 22 bytes (from end of data block)
		//  - write 12 bytes of zero
		//  - write A1 A1 A1 FB   (or F8)
		// But ATM we reuse the previous location of the data block
	}
	crc.init<0xA1, 0xA1, 0xA1, 0xFB>();

	// wait till sector is actually rotated under head
	schedule(FSM_TYPE2_ROTATED, next);
}

void WD2793::type2Rotated(EmuTime::param time)
{
	switch (commandReg & 0xF0) {
	case 0x80: // read sector
	case 0x90: // read sector (multi)
		startReadSector(time);
		break;

	case 0xA0: // write sector
	case 0xB0: // write sector (multi)
		// TODO By now the CPU should already have written the first
		// byte, otherwise the write sector command doesn't even start.
		// This is not yet implemented.
		drqTime.reset(time); // DRQ = true

		// Moment in time when the sector will be written (whether the
		// CPU wrote all required data or not)
		schedule(FSM_WRITE_SECTOR, drqTime + dataAvailable);
		break;
	}
}

void WD2793::startReadSector(EmuTime::param time)
{
	drqTime.reset(time);
	drqTime += 1; // (first) byte can be read in a moment
}

void WD2793::doneWriteSector()
{
	try {
		// any lost data?
		while (dataAvailable) {
			statusReg |= LOST_DATA;
			trackData.write(dataCurrent++, 0);
			crc.update(0);
			dataAvailable--;
		}

		// write 2 CRC bytes (big endian)
		trackData.write(dataCurrent++, crc.getValue() >> 8);
		trackData.write(dataCurrent++, crc.getValue() & 0xFF);
		// write one byte of 0xFE
		// TODO check this, datasheet is not very clear about this
		trackData.write(dataCurrent++, 0xFE);

		// write sector (actually full track) to disk.
		drive.writeTrack(trackData);

		if (!(commandReg & M_FLAG)) {
			endCmd();
		} else {
			// TODO multi sector write
			sectorReg++;
			endCmd();
		}
	} catch (MSXException&) {
		// Backend couldn't write data
		// TODO which status bit should be set?
		statusReg |= RECORD_NOT_FOUND;
		endCmd();
	}
}


void WD2793::startType3Cmd(EmuTime::param time)
{
	//PRT_DEBUG("WD2793 start type 3 command");
	statusReg &= ~(LOST_DATA | RECORD_NOT_FOUND | RECORD_TYPE);
	statusReg |= BUSY;

	if (!drive.isDiskInserted()) {
		endCmd();
	} else {
		drive.setHeadLoaded(true, time);
		// WD2795/WD2797 would now set SSO output

		if (commandReg & E_FLAG) {
			schedule(FSM_TYPE3_WAIT_LOAD,
			         time + EmuDuration::msec(30)); // when 1MHz clock
		} else {
			type3WaitLoad(time);
		}
	}
}

void WD2793::type3WaitLoad(EmuTime::param time)
{
	// TODO wait till head loaded, I arbitrarily took 1ms delay
	schedule(FSM_TYPE3_LOADED, time + EmuDuration::msec(1));
}

void WD2793::type3Loaded(EmuTime::param time)
{
	// TODO TG43 update
	if (((commandReg & 0xF0) == 0xF0) && (drive.isWriteProtected())) {
		// write track command and write protected
		statusReg |= WRITE_PROTECTED;
		endCmd();
		return;
	}

	EmuTime next(EmuTime::dummy());
	if ((commandReg & 0xF0) == 0xC0) {
		// read address
		try {
			// wait till next sector header
			RawTrack::Sector sector;
			next = drive.getNextSector(time, trackData, sector);
			setDrqRate();
			if (next == EmuTime::infinity) {
				// TODO wait for 5 revolutions
				statusReg |= RECORD_NOT_FOUND;
				endCmd();
				return;
			}
			dataCurrent = sector.addrIdx;
			dataAvailable = 6;
		} catch (MSXException& e) {
			PRT_DEBUG("WD2793: read addr failed: " << e.getMessage()); (void)&e;
			statusReg |= RECORD_NOT_FOUND;
			endCmd();
			return;
		}
	} else {
		// read/write track
		// wait till next index pulse
		next = drive.getTimeTillIndexPulse(time);
	}
	schedule(FSM_TYPE3_ROTATED, next);
}

void WD2793::type3Rotated(EmuTime::param time)
{
	switch (commandReg & 0xF0) {
	case 0xC0: // read Address
		readAddressCmd(time);
		break;
	case 0xE0: // read track
		readTrackCmd(time);
		break;
	case 0xF0: // write track
		writeTrackCmd(time);
		break;
	}
}

void WD2793::readAddressCmd(EmuTime::param time)
{
	drqTime.reset(time);
	drqTime += 1; // (first) byte can be read in a moment
}

void WD2793::readTrackCmd(EmuTime::param time)
{
	try {
		drive.readTrack(trackData);
		setDrqRate();
		dataCurrent = 0;
		dataAvailable = trackData.getLength();
		drqTime.reset(time);
		drqTime += 1; // (first) byte can be read in a moment
	} catch (MSXException& e) {
		PRT_DEBUG("WD2793: read track failed: " << e.getMessage()); (void)&e;
		// TODO status bits?
		endCmd();
	}
}

void WD2793::writeTrackCmd(EmuTime::param time)
{
	// TODO By now the CPU should already have written the first byte,
	// otherwise the write track command doesn't even start. This is not
	// yet implemented.
	try {
		// The _only_ reason we call readTrack() is to get the track
		// length of the existing track. Ideally we should just
		// overwrite the track with another length. But the DMK file
		// format cannot handle tracks with different lengths.
		drive.readTrack(trackData);
	} catch (MSXException& /*e*/) {
		endCmd();
	}
	trackData.clear(trackData.getLength());
	setDrqRate();
	dataCurrent = 0;
	dataAvailable = trackData.getLength();
	drqTime.reset(time); // DRQ = true
	lastWasA1 = false;

	// Moment in time when the track will be written (whether the CPU wrote
	// all required data or not).
	schedule(FSM_WRITE_TRACK, drqTime + dataAvailable);
}

void WD2793::doneWriteTrack()
{
	try {
		// any lost data?
		while (dataAvailable) {
			statusReg |= LOST_DATA;
			trackData.write(dataCurrent, 0);
			dataCurrent++;
			dataAvailable--;
		}
		drive.writeTrack(trackData);
	} catch (MSXException&) {
		// Ignore. Should rarely happen, because
		// write-protected is already checked at the
		// beginning of write-track command (maybe
		// when disk is swapped during format)
	}
	endCmd();
}


void WD2793::startType4Cmd(EmuTime::param time)
{
	// Force interrupt
	PRT_DEBUG("WD2793 command: Force interrupt");

	byte flags = commandReg & 0x0F;
	if (flags & (N2R_IRQ | R2N_IRQ)) {
		// all flags not yet supported
		PRT_DEBUG("WD2793 type 4 cmd, unimplemented bits " << int(flags));
	}

	if (flags == 0x00) {
		immediateIRQ = false;
	}
	if ((flags & IDX_IRQ) && drive.isDiskInserted()) {
		setSyncPoint(drive.getTimeTillIndexPulse(time), SCHED_IDX_IRQ);
	} else {
		removeSyncPoint(SCHED_IDX_IRQ);
	}
	if (flags & IMM_IRQ) {
		immediateIRQ = true;
	}

	drqTime.reset(EmuTime::infinity); // DRQ = false
	statusReg &= ~BUSY; // reset status on Busy
}

void WD2793::endCmd()
{
	drqTime.reset(EmuTime::infinity); // DRQ = false
	setIRQ();
	statusReg &= ~BUSY;
}


static enum_string<WD2793::FSMState> fsmStateInfo[] = {
	{ "NONE",            WD2793::FSM_NONE },
	{ "SEEK",            WD2793::FSM_SEEK },
	{ "TYPE2_WAIT_LOAD", WD2793::FSM_TYPE2_WAIT_LOAD },
	{ "TYPE2_LOADED",    WD2793::FSM_TYPE2_LOADED },
	{ "TYPE2_ROTATED",   WD2793::FSM_TYPE2_ROTATED },
	{ "WRITE_SECTOR",    WD2793::FSM_WRITE_SECTOR },
	{ "TYPE3_WAIT_LOAD", WD2793::FSM_TYPE3_WAIT_LOAD },
	{ "TYPE3_LOADED",    WD2793::FSM_TYPE3_LOADED },
	{ "TYPE3_ROTATED",   WD2793::FSM_TYPE3_ROTATED },
	{ "WRITE_TRACK",     WD2793::FSM_WRITE_TRACK },
	{ "IDX_IRQ",         WD2793::FSM_IDX_IRQ }
};
SERIALIZE_ENUM(WD2793::FSMState, fsmStateInfo);

// version 1: initial version
// version 2: removed members: commandStart, DRQTimer, DRQ, transferring, formatting
//            added member: drqTime (has different semantics than DRQTimer)
//            also the timing of the data-transfer commands (read/write sector
//            and write track) has changed. So this could result in replay-sync
//            errors.
//            (Also the enum FSMState has changed, but that's not a problem.)
// version 3: Added members 'crc' and 'lastWasA1'.
//            Replaced 'dataBuffer' with 'trackData'. We don't attempt to migrate
//            the old 'dataBuffer' content to 'trackData' (doing so would be
//            quite difficult). This means that old savestates that were in the
//            middle of a sector/track read/write command probably won't work
//            correctly anymore. We do give a warning on this.
// version 4: changed type of drqTime from Clock to DynamicClock
template<typename Archive>
void WD2793::serialize(Archive& ar, unsigned version)
{
	ar.template serializeBase<Schedulable>(*this);

	ar.serialize("fsmState", fsmState);
	ar.serialize("statusReg", statusReg);
	ar.serialize("commandReg", commandReg);
	ar.serialize("sectorReg", sectorReg);
	ar.serialize("trackReg", trackReg);
	ar.serialize("dataReg", dataReg);

	ar.serialize("directionIn", directionIn);
	ar.serialize("INTRQ", INTRQ);
	ar.serialize("immediateIRQ", immediateIRQ);

	ar.serialize("dataCurrent", dataCurrent);
	ar.serialize("dataAvailable", dataAvailable);

	if (ar.versionAtLeast(version, 2)) {
		if (ar.versionAtLeast(version, 4)) {
			ar.serialize("drqTime", drqTime);
		} else {
			assert(ar.isLoader());
			Clock<6250 * 5> c(EmuTime::dummy());
			ar.serialize("drqTime", c);
			drqTime.reset(c.getTime());
			drqTime.setFreq(6250 * 5);
		}
	} else {
		assert(ar.isLoader());
		//ar.serialize("commandStart", commandStart);
		//ar.serialize("DRQTimer", DRQTimer);
		//ar.serialize("DRQ", DRQ);
		//ar.serialize("transferring", transferring);
		//ar.serialize("formatting", formatting);
		drqTime.reset(EmuTime::infinity);

		// Compared to version 1, the datatransfer commands are
		// implemented very differently. We don't attempt to restore
		// the correct state from the old savestate. But we do give a
		// warning.
		if ((statusReg & BUSY) &&
		    (((commandReg & 0xC0) == 0x80) ||  // read/write sector
		     ((commandReg & 0xF0) == 0xF0))) { // write track
			cliComm.printWarning(
				"Loading an old savestate that had an "
				"in-progress WD2793 data-transfer command. "
				"This is not fully backwards-compatible and "
				"could cause wrong emulation behavior.");
		}
	}

	if (ar.versionAtLeast(version, 3)) {
		ar.serialize("trackData", trackData);
		ar.serialize("lastWasA1", lastWasA1);
		word crcVal = crc.getValue();
		ar.serialize("crc", crcVal);
		crc.init(crcVal);
	} else {
		assert(ar.isLoader());
		//ar.serialize_blob("dataBuffer", dataBuffer, sizeof(dataBuffer));
		// Compared to version 1 or 2, the databuffer works different:
		// before we only stored the data of the logical sector, now
		// we store the full content of the raw track. We don't attempt
		// to migrate the old format to the new one (it's not very
		// easy). We only give a warning.
		if ((statusReg & BUSY) &&
		    (((commandReg & 0xC0) == 0x80) ||  // read/write sector
		     ((commandReg & 0xF0) == 0xF0))) { // write track
			cliComm.printWarning(
				"Loading an old savestate that had an "
				"in-progress WD2793 data-transfer command. "
				"This is not fully backwards-compatible and "
				"could cause wrong emulation behavior.");
		}
	}
}
INSTANTIATE_SERIALIZE_METHODS(WD2793);

} // namespace openmsx

