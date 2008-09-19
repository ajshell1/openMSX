// $Id$

#ifndef FILE_HH
#define FILE_HH

#include "openmsx.hh"
#include "noncopyable.hh"
#include <memory>
#include <string>
#include <ctime>

namespace openmsx {

class Filename;
class FileBase;

class File : private noncopyable
{
public:
	enum OpenMode {
		NORMAL,
		TRUNCATE,
		CREATE,
		LOAD_PERSISTENT,
		SAVE_PERSISTENT,
		PRE_CACHE,
	};

	/**
	 * Create file object and open underlying file.
	 * @param filename Name of the file to be opened.
	 * @param mode Mode to open the file in:
	 * @throws FileException
	 */
	explicit File(const std::string& filename, OpenMode mode = NORMAL);
	explicit File(const Filename&    filename, OpenMode mode = NORMAL);

	/** This constructor maps very closely on the fopen() libc function.
	  * Compared to constructor above, it does not transparantly
	  * uncompress files.
	  * @param filename Name of the file to be opened.
	  * @param mode Open mode, same meaning as in fopen(), but we assert
	  *             that it contains a 'b' character.
	  */
	File(const std::string& filename, const char* mode);
	File(const Filename&    filename, const char* mode);

	/**
	 * Destroy file object.
	 */
	~File();

	/**
	 * Read from file.
	 * @param buffer Destination address
	 * @param num Number of bytes to read
	 * @throws FileException
	 */
	void read(void* buffer, unsigned num);

	/**
	 * Write to file.
	 * @param buffer Source address
	 * @param num Number of bytes to write
	 * @throws FileException
	 */
	void write(const void* buffer, unsigned num);

	/**
	 * Map file in memory.
	 * @param writeBack Set to true if writes to the memory block
	 *              should also be written to the file. Note that
	 *              the file may only be updated when you munmap
	 *              again (may happen earlier but not guaranteed).
	 * @result Pointer to memory block.
	 * @throws FileException
	 */
	byte* mmap(bool writeBack = false);

	/**
	 * Unmap file from memory.
	 * @throws FileException
	 */
	void munmap();

	/**
	 * Returns the size of this file
	 * @result The size of this file
	 * @throws FileException
	 */
	unsigned getSize();

	/**
	 * Move read/write pointer to the specified position.
	 * @param pos Position in bytes from the beginning of the file.
	 * @throws FileException
	 */
	void seek(unsigned pos);

	/**
	 * Get the current position of the read/write pointer.
	 * @result Position in bytes from the beginning of the file.
	 * @throws FileException
	 */
	unsigned getPos();

	/**
	 * Truncate file size. Enlarging file size always works, but
	 * making file smaller doesn't work on some platforms (windows)
	 * @throws FileException
	 */
	void truncate(unsigned size);

	/**
	 * Force a write of all buffered data to disk. There is no need to
	 * call this function before destroying a File object.
	 */
	void flush();

	/**
	 * Returns the URL of this file object.
	 * @throws FileException
	 */
	const std::string getURL() const;

	/**
	 * Get Original filename for this object. This will usually just
	 * return the filename portion of the URL. However for compressed
	 * files this will be different.
	 * @result Original file name
	 * @throws FileException
	 */
	const std::string getOriginalName();

	/**
	 * Check if this file is readonly
	 * @result true iff file is readonly
	 * @throws FileException
	 */
	bool isReadOnly() const;

	/**
	 * Get the date/time of last modification
	 * @throws FileException
	 */
	time_t getModificationDate();

private:
	friend class LocalFileReference;
	/** This is an internal method used by LocalFileReference.
	 * Returns the path to the (uncompressed) file on the local,
	 * filesystem. Or an empty string in case there is no such path.
	 */
	const std::string getLocalReference() const;

	const std::auto_ptr<FileBase> file;
};

} // namespace openmsx

#endif
