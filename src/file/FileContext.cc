// $Id$

#include "FileContext.hh"
#include "FileOperations.hh"
#include "FileException.hh"
#include "GlobalSettings.hh"
#include "CommandController.hh"
#include "CliComm.hh"
#include "CommandException.hh"
#include "StringSetting.hh"
#include "StringOp.hh"
#include "serialize.hh"
#include "serialize_stl.hh"
#include "openmsx.hh"
#include <cassert>

using std::string;
using std::vector;

namespace openmsx {

const string USER_DIRS    = "{{USER_DIRS}}";
const string USER_OPENMSX = "{{USER_OPENMSX}}";
const string USER_DATA    = "{{USER_DATA}}";
const string SYSTEM_DATA  = "{{SYSTEM_DATA}}";


static void getUserDirs(CommandController& controller, vector<string>& result)
{
	assert(&controller);
	try {
		const string& list = controller.getGlobalSettings().
			getUserDirSetting().getValue();
		vector<string> dirs;
		controller.splitList(list, dirs);
		result.insert(result.end(), dirs.begin(), dirs.end());
	} catch (CommandException& e) {
		controller.getCliComm().printWarning(
			"user directories: " + e.getMessage());
	}
}

static string subst(const string& path, const string& before,
                    const string& after)
{
	assert(StringOp::startsWith(path, before));
	return after + path.substr(before.size());
}

static vector<string> getPathsHelper(CommandController& controller,
                                     const vector<string>& input)
{
	vector<string> result;
	for (vector<string>::const_iterator it = input.begin();
	     it != input.end(); ++it) {
		if (StringOp::startsWith(*it, USER_OPENMSX)) {
			result.push_back(subst(*it, USER_OPENMSX,
			                       FileOperations::getUserOpenMSXDir()));
		} else if (StringOp::startsWith(*it, USER_DATA)) {
			result.push_back(subst(*it, USER_DATA,
			                       FileOperations::getUserDataDir()));
		} else if (StringOp::startsWith(*it, SYSTEM_DATA)) {
			result.push_back(subst(*it, SYSTEM_DATA,
			                       FileOperations::getSystemDataDir()));
		} else if (*it == USER_DIRS) {
			getUserDirs(controller, result);
		} else {
			result.push_back(*it);
		}
	}
	return result;
}

static string resolveHelper(const vector<string>& pathList,
                            const string& filename)
{
	PRT_DEBUG("Context: " << filename);
	string filepath = FileOperations::expandCurrentDirFromDrive(filename);
	filepath = FileOperations::expandTilde(filepath);
	if (FileOperations::isAbsolutePath(filepath)) {
		// absolute path, don't resolve
		return filepath;
	}

	for (vector<string>::const_iterator it = pathList.begin();
	     it != pathList.end(); ++it) {
		string name = FileOperations::join(*it, filename);
		name = FileOperations::expandTilde(name);
		PRT_DEBUG("Context: try " << name);
		if (FileOperations::exists(name)) {
			return name;
		}
	}
	// not found in any path
	throw FileException(filename + " not found in this context");
}

const string FileContext::resolve(CommandController& controller,
                                  const string& filename)
{
	vector<string> pathList = getPathsHelper(controller, paths);
	string result = resolveHelper(pathList, filename);
	assert(FileOperations::expandTilde(result) == result);
	return result;
}

const string FileContext::resolveCreate(const string& filename)
{
	string result;
	CommandController* controller = NULL;
	vector<string> pathList = getPathsHelper(*controller, savePaths);
	try {
		result = resolveHelper(pathList, filename);
	} catch (FileException& e) {
		string path = pathList.front();
		try {
			FileOperations::mkdirp(path);
		} catch (FileException& e) {
			PRT_DEBUG(e.getMessage());
		}
		result = FileOperations::join(path, filename);
	}
	assert(FileOperations::expandTilde(result) == result);
	return result;
}

vector<string> FileContext::getPaths(CommandController& controller) const
{
	return getPathsHelper(controller, paths);
}

///

ConfigFileContext::ConfigFileContext(const string& path,
                                     const string& hwDescr,
                                     const string& userName)
{
	paths.push_back(path);
	savePaths.push_back(FileOperations::join(
		USER_OPENMSX, "persistent", hwDescr, userName));
}

SystemFileContext::SystemFileContext()
{
	paths.push_back(USER_DATA);
	paths.push_back(SYSTEM_DATA);
	savePaths.push_back(USER_DATA);
}

OnlySystemFileContext::OnlySystemFileContext()
{
	paths.push_back(SYSTEM_DATA);
}

UserFileContext::UserFileContext(const string& savePath)
{
	paths.push_back("");
	paths.push_back(USER_DIRS);

	if (!savePath.empty()) {
		savePaths.push_back(FileOperations::join(
			USER_OPENMSX, "persistent", savePath));
	}
}

CurrentDirFileContext::CurrentDirFileContext()
{
	paths.push_back("");
}


template<typename Archive>
void FileContext::serialize(Archive& ar, unsigned /*version*/)
{
	ar.serialize("paths", paths);
	ar.serialize("savePaths", savePaths);
}
INSTANTIATE_SERIALIZE_METHODS(FileContext);

} // namespace openmsx
