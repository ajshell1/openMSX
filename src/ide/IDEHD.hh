// $Id$

#ifndef IDEHD_HH
#define IDEHD_HH

#include "AbstractIDEDevice.hh"
#include "SectorAccessibleDisk.hh"
#include "DiskContainer.hh"
#include "noncopyable.hh"
#include <string>
#include <memory>

namespace openmsx {

class MSXMotherBoard;
class XMLElement;
class DiskManipulator;
class File;
class HDCommand;

class IDEHD : public AbstractIDEDevice, public SectorAccessibleDisk,
              public DiskContainer, private noncopyable
{
public:
	IDEHD(MSXMotherBoard& motherBoard, const XMLElement& config,
	      const EmuTime& time);
	virtual ~IDEHD();

	// SectorAccessibleDisk:
	virtual void readLogicalSector(unsigned sector, byte* buf);
	virtual void writeLogicalSector(unsigned sector, const byte* buf);
	virtual unsigned getNbSectors() const;

	// Diskcontainer:
	virtual SectorAccessibleDisk* getSectorAccessibleDisk();
	virtual const std::string& getContainerName() const;

protected:
	// AbstractIDEDevice:
	virtual bool isPacketDevice();
	virtual const std::string& getDeviceName();
	virtual void fillIdentifyBlock(byte* buffer);
	virtual unsigned readBlockStart(byte* buffer, unsigned count);
	virtual void writeBlockComplete(byte* buffer, unsigned count);
	virtual void executeCommand(byte cmd);

private:
	MSXMotherBoard& motherBoard;
	DiskManipulator& diskManipulator;
	std::string name;
	std::auto_ptr<HDCommand> hdCommand;
	std::auto_ptr<File> file;
	unsigned transferSectorNumber;

	friend class HDCommand;
};

} // namespace openmsx

#endif
