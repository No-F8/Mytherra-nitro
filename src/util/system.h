// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * thread wrappers, startup time
 */
#ifndef MYTHERRA_UTIL_SYSTEM_H
#define MYTHERRA_UTIL_SYSTEM_H

#if defined(HAVE_CONFIG_H)
#include <config/mytherra-config.h>
#endif

#include <compat/assumptions.h>
#include <compat/compat.h>
#include <logging.h>
#include <sync.h>
#include <util/fs.h>
#include <util/settings.h>
#include <util/time.h>

#include <any>
#include <map>
#include <optional>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

class ArgsManager;
class UniValue;

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

extern const char * const MYTHERRA_CONF_FILENAME;
extern const char * const MYTHERRA_SETTINGS_FILENAME;

void SetupEnvironment();
bool SetupNetworking();
// Return true if -datadir option points to a valid directory or is not specified.
bool CheckDataDirOption(const ArgsManager& args);
fs::path GetConfigFile(const ArgsManager& args, const fs::path& configuration_file_path);
#ifndef WIN32
std::string ShellEscape(const std::string& arg);
#endif
#if HAVE_SYSTEM
void runCommand(const std::string& strCommand);
#endif

/**
 * Most paths passed as configuration arguments are treated as relative to
 * the datadir if they are not absolute.
 *
 * @param args Parsed arguments and settings.
 * @param path The path to be conditionally prefixed with datadir.
 * @param net_specific Use network specific datadir variant
 * @return The normalized path.
 */
fs::path AbsPathForConfigVal(const ArgsManager& args, const fs::path& path, bool net_specific = true);

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

enum class OptionsCategory {
    OPTIONS,
    CONNECTION,
    WALLET,
    WALLET_DEBUG_TEST,
    ZMQ,
    DEBUG_TEST,
    CHAINPARAMS,
    NODE_RELAY,
    BLOCK_CREATION,
    RPC,
    GUI,
    COMMANDS,
    REGISTER_COMMANDS,

    HIDDEN // Always the last option to avoid printing these in the help
};

struct SectionInfo
{
    std::string m_name;
    std::string m_file;
    int m_line;
};

std::string SettingToString(const util::SettingsValue&, const std::string&);
std::optional<std::string> SettingToString(const util::SettingsValue&);

int64_t SettingToInt(const util::SettingsValue&, int64_t);
std::optional<int64_t> SettingToInt(const util::SettingsValue&);

bool SettingToBool(const util::SettingsValue&, bool);
std::optional<bool> SettingToBool(const util::SettingsValue&);

class ArgsManager
{
public:
    /**
     * Flags controlling how config and command line arguments are validated and
     * interpreted.
     */
    enum Flags : uint32_t {
        ALLOW_ANY = 0x01,         //!< disable validation
        // ALLOW_BOOL = 0x02,     //!< unimplemented, draft implementation in #16545
        // ALLOW_INT = 0x04,      //!< unimplemented, draft implementation in #16545
        // ALLOW_STRING = 0x08,   //!< unimplemented, draft implementation in #16545
        // ALLOW_LIST = 0x10,     //!< unimplemented, draft implementation in #16545
        DISALLOW_NEGATION = 0x20, //!< disallow -nofoo syntax
        DISALLOW_ELISION = 0x40,  //!< disallow -foo syntax that doesn't assign any value

        DEBUG_ONLY = 0x100,
        /* Some options would cause cross-contamination if values for
         * mainnet were used while running on regtest/testnet (or vice-versa).
         * Setting them as NETWORK_ONLY ensures that sharing a config file
         * between mainnet and regtest/testnet won't cause problems due to these
         * parameters by accident. */
        NETWORK_ONLY = 0x200,
        // This argument's value is sensitive (such as a password).
        SENSITIVE = 0x400,
        COMMAND = 0x800,
    };

protected:
    struct Arg
    {
        std::string m_help_param;
        std::string m_help_text;
        unsigned int m_flags;
    };

    mutable RecursiveMutex cs_args;
    util::Settings m_settings GUARDED_BY(cs_args);
    std::vector<std::string> m_command GUARDED_BY(cs_args);
    std::string m_network GUARDED_BY(cs_args);
    std::set<std::string> m_network_only_args GUARDED_BY(cs_args);
    std::map<OptionsCategory, std::map<std::string, Arg>> m_available_args GUARDED_BY(cs_args);
    bool m_accept_any_command GUARDED_BY(cs_args){true};
    std::list<SectionInfo> m_config_sections GUARDED_BY(cs_args);
    mutable fs::path m_cached_blocks_path GUARDED_BY(cs_args);
    mutable fs::path m_cached_datadir_path GUARDED_BY(cs_args);
    mutable fs::path m_cached_network_datadir_path GUARDED_BY(cs_args);

    [[nodiscard]] bool ReadConfigStream(std::istream& stream, const std::string& filepath, std::string& error, bool ignore_invalid_keys = false);

    /**
     * Returns true if settings values from the default section should be used,
     * depending on the current network and whether the setting is
     * network-specific.
     */
    bool UseDefaultSection(const std::string& arg) const EXCLUSIVE_LOCKS_REQUIRED(cs_args);

 public:
    /**
     * Get setting value.
     *
     * Result will be null if setting was unset, true if "-setting" argument was passed
     * false if "-nosetting" argument was passed, and a string if a "-setting=value"
     * argument was passed.
     */
    util::SettingsValue GetSetting(const std::string& arg) const;

    /**
     * Get list of setting values.
     */
    std::vector<util::SettingsValue> GetSettingsList(const std::string& arg) const;

    ArgsManager();
    ~ArgsManager();

    /**
     * Select the network in use
     */
    void SelectConfigNetwork(const std::string& network);

    [[nodiscard]] bool ParseParameters(int argc, const char* const argv[], std::string& error);

    /**
     * Return config file path (read-only)
     */
    fs::path GetConfigFilePath() const;
    [[nodiscard]] bool ReadConfigFiles(std::string& error, bool ignore_invalid_keys = false);

    /**
     * Log warnings for options in m_section_only_args when
     * they are specified in the default section but not overridden
     * on the command line or in a network-specific section in the
     * config file.
     */
    std::set<std::string> GetUnsuitableSectionOnlyArgs() const;

    /**
     * Log warnings for unrecognized section names in the config file.
     */
    std::list<SectionInfo> GetUnrecognizedSections() const;

    struct Command {
        /** The command (if one has been registered with AddCommand), or empty */
        std::string command;
        /**
         * If command is non-empty: Any args that followed it
         * If command is empty: The unregistered command and any args that followed it
         */
        std::vector<std::string> args;
    };
    /**
     * Get the command and command args (returns std::nullopt if no command provided)
     */
    std::optional<const Command> GetCommand() const;

    /**
     * Get blocks directory path
     *
     * @return Blocks path which is network specific
     */
    const fs::path& GetBlocksDirPath() const;

    /**
     * Get data directory path
     *
     * @return Absolute path on success, otherwise an empty path when a non-directory path would be returned
     */
    const fs::path& GetDataDirBase() const { return GetDataDir(false); }

    /**
     * Get data directory path with appended network identifier
     *
     * @return Absolute path on success, otherwise an empty path when a non-directory path would be returned
     */
    const fs::path& GetDataDirNet() const { return GetDataDir(true); }

    /**
     * Clear cached directory paths
     */
    void ClearPathCache();

    /**
     * Return a vector of strings of the given argument
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return command-line arguments
     */
    std::vector<std::string> GetArgs(const std::string& strArg) const;

    /**
     * Return true if the given argument has been manually set
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return true if the argument has been set
     */
    bool IsArgSet(const std::string& strArg) const;

    /**
     * Return true if the argument was originally passed as a negated option,
     * i.e. -nofoo.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return true if the argument was passed negated
     */
    bool IsArgNegated(const std::string& strArg) const;

    /**
     * Return string argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param strDefault (e.g. "1")
     * @return command-line argument or default value
     */
    std::string GetArg(const std::string& strArg, const std::string& strDefault) const;
    std::optional<std::string> GetArg(const std::string& strArg) const;

    /**
     * Return path argument or default value
     *
     * @param arg Argument to get a path from (e.g., "-datadir", "-blocksdir" or "-walletdir")
     * @param default_value Optional default value to return instead of the empty path.
     * @return normalized path if argument is set, with redundant "." and ".."
     * path components and trailing separators removed (see patharg unit test
     * for examples or implementation for details). If argument is empty or not
     * set, default_value is returned unchanged.
     */
    fs::path GetPathArg(std::string arg, const fs::path& default_value = {}) const;

    /**
     * Return integer argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param nDefault (e.g. 1)
     * @return command-line argument (0 if invalid number) or default value
     */
    int64_t GetIntArg(const std::string& strArg, int64_t nDefault) const;
    std::optional<int64_t> GetIntArg(const std::string& strArg) const;

    /**
     * Return boolean argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param fDefault (true or false)
     * @return command-line argument or default value
     */
    bool GetBoolArg(const std::string& strArg, bool fDefault) const;
    std::optional<bool> GetBoolArg(const std::string& strArg) const;

    /**
     * Set an argument if it doesn't already have a value
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param strValue Value (e.g. "1")
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetArg(const std::string& strArg, const std::string& strValue);

    /**
     * Set a boolean argument if it doesn't already have a value
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param fValue Value (e.g. false)
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetBoolArg(const std::string& strArg, bool fValue);

    // Forces an arg setting. Called by SoftSetArg() if the arg hasn't already
    // been set. Also called directly in testing.
    void ForceSetArg(const std::string& strArg, const std::string& strValue);

    /**
     * Returns the appropriate chain name from the program arguments.
     * @return CBaseChainParams::MAIN by default; raises runtime error if an invalid combination is given.
     */
    std::string GetChainName() const;

    /**
     * Add argument
     */
    void AddArg(const std::string& name, const std::string& help, unsigned int flags, const OptionsCategory& cat);

    /**
     * Add subcommand
     */
    void AddCommand(const std::string& cmd, const std::string& help);

    /**
     * Add many hidden arguments
     */
    void AddHiddenArgs(const std::vector<std::string>& args);

    /**
     * Clear available arguments
     */
    void ClearArgs() {
        LOCK(cs_args);
        m_available_args.clear();
        m_network_only_args.clear();
    }

    /**
     * Get the help string
     */
    std::string GetHelpMessage() const;

    /**
     * Return Flags for known arg.
     * Return nullopt for unknown arg.
     */
    std::optional<unsigned int> GetArgFlags(const std::string& name) const;

    /**
     * Get settings file path, or return false if read-write settings were
     * disabled with -nosettings.
     */
    bool GetSettingsPath(fs::path* filepath = nullptr, bool temp = false, bool backup = false) const;

    /**
     * Read settings file. Push errors to vector, or log them if null.
     */
    bool ReadSettingsFile(std::vector<std::string>* errors = nullptr);

    /**
     * Write settings file or backup settings file. Push errors to vector, or
     * log them if null.
     */
    bool WriteSettingsFile(std::vector<std::string>* errors = nullptr, bool backup = false) const;

    /**
     * Get current setting from config file or read/write settings file,
     * ignoring nonpersistent command line or forced settings values.
     */
    util::SettingsValue GetPersistentSetting(const std::string& name) const;

    /**
     * Access settings with lock held.
     */
    template <typename Fn>
    void LockSettings(Fn&& fn)
    {
        LOCK(cs_args);
        fn(m_settings);
    }

    /**
     * Log the config file options and the command line arguments,
     * useful for troubleshooting.
     */
    void LogArgs() const;

private:
    /**
     * Get data directory path
     *
     * @param net_specific Append network identifier to the returned path
     * @return Absolute path on success, otherwise an empty path when a non-directory path would be returned
     */
    const fs::path& GetDataDir(bool net_specific) const;

    // Helper function for LogArgs().
    void logArgsPrefix(
        const std::string& prefix,
        const std::string& section,
        const std::map<std::string, std::vector<util::SettingsValue>>& args) const;
};

extern ArgsManager gArgs;

/**
 * @return true if help has been requested via a command-line arg
 */
bool HelpRequested(const ArgsManager& args);

/** Add help options to the args manager */
void SetupHelpOptions(ArgsManager& args);

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string& message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string& option, const std::string& message);

/**
 * Return the number of cores available on the current system.
 * @note This does count virtual cores, such as those provided by HyperThreading.
 */
int GetNumCores();

/**
 * On platforms that support it, tell the kernel the calling thread is
 * CPU-intensive and non-interactive. See SCHED_BATCH in sched(7) for details.
 *
 */
void ScheduleBatchPriority();

namespace util {

//! Simplification of std insertion
template <typename Tdst, typename Tsrc>
inline void insert(Tdst& dst, const Tsrc& src) {
    dst.insert(dst.begin(), src.begin(), src.end());
}
template <typename TsetT, typename Tsrc>
inline void insert(std::set<TsetT>& dst, const Tsrc& src) {
    dst.insert(src.begin(), src.end());
}

/**
 * Helper function to access the contained object of a std::any instance.
 * Returns a pointer to the object if passed instance has a value and the type
 * matches, nullptr otherwise.
 */
template<typename T>
T* AnyPtr(const std::any& any) noexcept
{
    T* const* ptr = std::any_cast<T*>(&any);
    return ptr ? *ptr : nullptr;
}

#ifdef WIN32
class WinCmdLineArgs
{
public:
    WinCmdLineArgs();
    ~WinCmdLineArgs();
    std::pair<int, char**> get();

private:
    int argc;
    char** argv;
    std::vector<std::string> args;
};
#endif

} // namespace util

#endif // MYTHERRA_UTIL_SYSTEM_H
