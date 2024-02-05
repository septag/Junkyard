/*

RemedyBG driver for 0.3.9.1 and later.

Note that the following documentation is preliminary and is subject to change.

The RemedyBG driver on Windows uses named pipes for communication between
processes. To enable this feature, RemedyBG can be invoked with the
"--servername" argument, passing the base name used for the creation of the
pipes. Without this argument, no named pipes will be created.

There are two named pipes created when the "--servername basename" argument is
given: one named ""\\.\pipe\basename", the debug control pipe, and another named
"\\.\pipe\basename-events", the debug events pipe.

The debug control pipe is a read-write pipe that should be setup in message mode
and can be used to control the debugger, including things such as creating a
session, adding a breakpoint, or deleting an expression from a watch window.

The debug control pipe command pipe accepts a packed stream of data beginning
with a 2 byte rdbg_Command. Depending on the command, one or more arguments to
the command may be required. See the documentation for individual commands in
the rdbg_Command enumeration below.

All commands will first return a rdbg_CommandResult followed by zero or more
additional values depending on the command.

The debug events pipe is a secondary, read-only pipe that can be used to
receive notifications of various events such as a breakpoint being hit. It, like
the debug control pipe, will use a packed stream of data. The format of the data
is documented in the rdbg_DebugEventKind enumeration below.

Note that to aid in debugging, you can view the RemedyBG error log at
`%APPDATA%\remedybg\app.log`.

*/

#pragma once

#include <stdint.h>

#define RDBG_MAX_SERVERNAME_LEN 64

typedef uint8_t rdbg_Bool;

// A rolling 32-bit integer is used for any command that takes or returns a UID.
// These UIDs are never persisted and as such, can change between runs of
// RemedyBG. Zero will never be a valid id.
typedef uint32_t rdbg_Id;

// A string consists of a length followed by an UTF-8 encoded character array of
// 'length' bytes. Strings are never nul-terminated.
#pragma warning(push)
#pragma warning(disable: 4200)
#pragma pack(push, 1)
struct rdbg_String
{
   uint16_t len;
   uint8_t data[0];
};
#pragma pack(pop)
#pragma warning(pop)

enum rdbg_CommandResult
{
   RDBG_COMMAND_RESULT_UNKNOWN = 0,

   RDBG_COMMAND_RESULT_OK = 1,

   // Generic failure
   RDBG_COMMAND_RESULT_FAIL = 2,

   // Result if the command is aborted due to a specified behavior and
   // condition including RDBG_IF_DEBUGGING_TARGET_ABORT_COMMAND or
   // RDBG_IF_SESSION_IS_MODIFIED_ABORT_COMMAND. The result can also be returned
   // if an unnamed session is saved, prompts for a filename, and the user
   // cancels this operation.
   RDBG_COMMAND_RESULT_ABORTED = 3,

   // Result if the given command buffer given is less than 2 bytes or if the
   // command is not one of the enumerated commands in rdbg_Command.
   RDBG_COMMAND_RESULT_INVALID_COMMAND = 4,

   // Result if the response generated is too large to fit in the buffer.
   RDBG_COMMAND_RESULT_BUFFER_TOO_SMALL = 5,

   // Result if an opening a file (i.e., a session, text file).
   RDBG_COMMAND_RESULT_FAILED_OPENING_FILE = 6,

   // Result if saving a session fails.
   RDBG_COMMAND_RESULT_FAILED_SAVING_SESSION = 7,

   // Result if the given ID is invalid.
   RDBG_COMMAND_RESULT_INVALID_ID = 8,

   // Result if a command expects the target to be in a particular state (not
   // debugging, debugging and suspended, or debugging and executing) and is
   // not.
   RDBG_COMMAND_RESULT_INVALID_TARGET_STATE = 9,

   // Result if an active configuration does not exist
   RDBG_COMMAND_RESULT_FAILED_NO_ACTIVE_CONFIG = 10,

   // Result if the command does not apply to given breakpoint's kind
   RDBG_COMMAND_RESULT_INVALID_BREAKPOINT_KIND = 11,
};

// Commands that take an rdbg_DebuggingTargetBehavior can specify what should
// happen in the case the target is being debugged.
enum rdbg_DebuggingTargetBehavior
{
   RDBG_IF_DEBUGGING_TARGET_STOP_DEBUGGING = 1,
   RDBG_IF_DEBUGGING_TARGET_ABORT_COMMAND = 2
};

// Commands that take an rdbg_ModifiedSessionBehavior can specify what should
// happen when there is an open, modified session.
enum rdbg_ModifiedSessionBehavior
{
   RDBG_IF_SESSION_IS_MODIFIED_SAVE_AND_CONTINUE = 1,
   RDBG_IF_SESSION_IS_MODIFIED_CONTINUE_WITHOUT_SAVING = 2,
   RDBG_IF_SESSION_IS_MODIFIED_ABORT_COMMAND = 3,
};

enum rdbg_TargetState
{
   RDBG_TARGET_STATE_NONE = 1,
   RDBG_TARGET_STATE_SUSPENDED = 2,
   RDBG_TARGET_STATE_EXECUTING = 3,
};

enum rdbg_BreakpointKind
{
   RDBG_BREAKPOINT_KIND_FUNCTION_NAME = 1,
   RDBG_BREAKPOINT_KIND_FILENAME_LINE = 2,
   RDBG_BREAKPOINT_KIND_ADDRESS = 3,
   RDBG_BREAKPOINT_KIND_PROCESSOR = 4,
};

enum rdbg_ProcessorBreakpointAccessKind
{
   RDBG_PROCESSOR_BREAKPOINT_ACCESS_KIND_WRITE = 1,
   RDBG_PROCESSOR_BREAKPOINT_ACCESS_KIND_READ_WRITE = 2,
   RDBG_PROCESSOR_BREAKPOINT_ACCESS_KIND_EXECUTE = 3,
};

enum rdbg_Command
{
   // Bring the RemedyBG window to the foreground and activate it. No additional
   // arguments follow the command. Returns RDBG_COMMAND_RESULT_OK or
   // RDBG_COMMAND_RESULT_FAIL.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_BRING_DEBUGGER_TO_FOREGROUND = 50,

   // Set the size and position of the RemedyBG window.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [x :: int32_t]
   // [y :: int32_t]
   // [width :: int32_t]
   // [height :: int32_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SET_WINDOW_POS = 51,

   // Get the size and position of the RemedyBG window.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [x :: int32_t]
   // [y :: int32_t]
   // [width :: int32_t]
   // [height :: int32_t]
   // [is_maximized: rdbg_Bool]
   RDBG_COMMAND_GET_WINDOW_POS = 52,
   
   // Set whether to automatically bring the debugger to the foreground whenever
   // the target is suspended (breakpoint hit, exception, single-step complete,
   // etc.). Defaults to true if not set.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bring_to_foreground_on_suspended :: rdbg_Bool (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SET_BRING_TO_FOREGROUND_ON_SUSPENDED = 53,

   // Exit the RemedyBG application.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [dtb :: rdbg_DebuggingTargetBehavior (uint8_t)]
   // [msb :: rdbg_ModifiedSessionBehavior (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_EXIT_DEBUGGER = 75,

   // Session
   //

   // Returns whether the current session is modified, or "dirty".
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [modified :: rdbg_Bool (uint8_t)]
   RDBG_COMMAND_GET_IS_SESSION_MODIFIED = 100,

   // Returns the current session's filename. If the filename has not been set
   // for the session then the result will be
   // RDBG_COMMAND_RESULT_UNNAMED_SESSION and the length of |filename| will be
   // zero.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [filename :: rdbg_String]
   RDBG_COMMAND_GET_SESSION_FILENAME = 101,

   // Creates a new session. All configurations are cleared and reset.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [dtb :: rdbg_DebuggingTargetBehavior (uint8_t)]
   // [msb :: rdbg_ModifiedSessionBehavior (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_NEW_SESSION = 102,

   // Open a session with the given filename.
   //
   // [command :: rdbg_Command (uint16_t)]
   // [dtb :: rdbg_DebuggingTargetBehavior (uint8_t)]
   // [msb :: rdbg_ModifiedSessionBehavior (uint8_t)]
   // [filename :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_OPEN_SESSION = 103,

   // Save session with its current filename. If the filename is has not been
   // specified for the session the user will be prompted. To save with a
   // filename see RDBG_COMMAND_SAVE_AS_SESSION, instead.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SAVE_SESSION = 104,

   // Save session with a given filename.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [filename :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SAVE_AS_SESSION = 105,

   // Retrieve a list of configurations for the current session.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_configs :: uint16_t]
   // .FOR(num_configs) {
   //   [uid :: rdbg_Id (uint32_t)]
   //   [command :: rdbg_String]
   //   [command_args :: rdbg_String]
   //   [working_dir :: rdbg_String]
   //   [environment_vars :: rdbg_String]
   //   [inherit_environment_vars_from_parent :: rdbg_Bool]
   //   [break_at_nominal_entry_point :: rdbg_Bool]
   //   [name :: rdbg_String]
   // }
   RDBG_COMMAND_GET_SESSION_CONFIGS = 106,

   // Add a new session configuration to the current session. All string
   // parameters accept zero length strings. Multiple environment variables
   // should be newline, '\n', separated. Returns the a unique ID for the
   // configuration.
   //
   // Note that 'name' is currently optional.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // [command :: rdbg_String]
   // [command_args :: rdbg_String]
   // [working_dir :: rdbg_String]
   // [environment_vars :: rdbg_String]
   // [inherit_environment_vars_from_parent :: rdbg_Bool]
   // [break_at_nominal_entry_point :: rdbg_Bool]
   // [name :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [uid :: rdbg_Id]
   RDBG_COMMAND_ADD_SESSION_CONFIG = 107,

   // Sets the active configuration for a session by configuration ID. If the
   // ID is not valid for the current session
   // RDBG_COMMAND_RESULT_INVALID_ID is returned.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // [id  :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SET_ACTIVE_SESSION_CONFIG = 108,

   // Deletes a session configuration by ID. If the ID is not valid for the
   // current session RDBG_COMMAND_REMOVE_SESSION_CONFIG is returned.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // [id  :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_SESSION_CONFIG = 109,

   // Deletes all session configurations in the current session.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_ALL_SESSION_CONFIGS = 110,

   // Source Files
   //

   // Opens the given file, if not already opened, and navigates to the
   // specified line number. The line number is optional and can be elided from
   // the command buffer. Returns result along with an ID for the file.
   //
   // [cmd :: rdbg_Command (uint16_t)
   // [filename :: rdbg_String]
   // [line_num :: uint32_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [id :: rdbg_Id]
   RDBG_COMMAND_GOTO_FILE_AT_LINE = 200,

   // Close the file with the given ID.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [id :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_CLOSE_FILE = 201,

   // Close all open files
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_CLOSE_ALL_FILES = 202,

   // Returns the current file. If no file is open, returns a zero ID,
   // zero-length filename, and zero line number.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [id :: rdbg_Id]
   // [filename :: rdbg_String]
   // [line_num :: uint32_t]
   RDBG_COMMAND_GET_CURRENT_FILE = 203,

   // Retrieve a list of open files.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_files :: uint16_t]
   // .FOR(num_files) {
   //   [id :: rdbg_Id]
   //   [filename :: rdbg_String]
   //   [line_num :: uint32_t]
   // }
   RDBG_COMMAND_GET_OPEN_FILES = 204,

   //
   // Debugger Control

   // Returns the target state for the current session.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [staste :: rdbg_TargetState (uint16_t)]
   RDBG_COMMAND_GET_TARGET_STATE = 300,

   // If the target is stopped, i.e., not currently being debugged, then start
   // debugging the active configuration. Setting break_at_entry to true will
   // stop execution at the at entry point specified in the configuration:
   // either the nominal entry point, such as "main" or "WinMain" or the entry
   // point function as described in the PE header. If the target is already
   // being debugged, this will return RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [break_at_entry_point :: rdbg_Bool (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_START_DEBUGGING = 301,

   // Stop debugging the target. If the target is not executing this will return
   // RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STOP_DEBUGGING = 302,

   // Restart debugging if the target is being debugging (either suspended or
   // executing) and the target was not attached to a process. Otherwise,
   // returns RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_RESTART_DEBUGGING = 303,

   // Attach to a process by the given process-id. The value of
   // |continue_execution| indicates whether the process should resume execution
   // after attached.  The debugger target behavior specifies what should happen
   // in the case when the target is being debugged (suspended or executing).
   // Can return: RDBG_COMMAND_RESULT_OK, RDBG_COMMAND_RESULT_FAIL, or
   // RDBG_COMMAND_RESULT_ABORT.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [process_id :: uint32_t]
   // [continue_execution :: rdbg_Bool]
   // [dtb :: rdbg_DebuggingTargetBehavior (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_ATTACH_TO_PROCESS_BY_PID = 304,

   // Attach to a process by the given name. The first process found, in the
   // case there are more than one with the same name, is used. The value of
   // |continue_execution| indicates whether the process should resume execution
   // after attached.  The debugger target behavior specifies what should happen
   // in the case when the target is being debugged (suspended or executing).
   // Can return: RDBG_COMMAND_RESULT_OK, RDBG_COMMAND_RESULT_FAIL, or
   // RDBG_COMMAND_RESULT_ABORT.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [process_name :: rdbg_String]
   // [continue_execution :: rdbg_Bool]
   // [dtb :: rdbg_DebuggingTargetBehavior (uint8_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_ATTACH_TO_PROCESS_BY_NAME = 305,

   // Detach from a target that is being debugged. Can return
   // RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DETACH_FROM_PROCESS = 306,

   // With the target suspended, step into by line. If a function call occurs,
   // this command will enter the function.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STEP_INTO_BY_LINE = 307,

   // With the target suspended, step into by instruction. If a function call
   // occurs, this command will enter the function. Can return
   // RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STEP_INTO_BY_INSTRUCTION = 308,

   // With the target suspended, step into by line. If a function call occurs,
   // this command step over that function and not enter it. Can return
   // return RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STEP_OVER_BY_LINE = 309,

   // With the target suspended, step into by instruction. If a function call
   // occurs, this command will step over that function and not enter it. Can
   // return RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STEP_OVER_BY_INSTRUCTION = 310,

   // With the target suspended, continue running to the call site of the
   // current function, i.e., step out.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_STEP_OUT = 311,

   // With the target suspended, continue execution. Can return
   // RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_CONTINUE_EXECUTION = 312,

   // When the target is not being debugged or is suspended, run to the given
   // filename and line number.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [filename :: rdbg_String]
   // [line_num :: uint32_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_RUN_TO_FILE_AT_LINE = 313,

   // Halt the execution of a target that is in the executing state. Can return
   // RDBG_COMMAND_RESULT_OK or RDBG_COMMAND_RESULT_INVALID_TARGET_STATE.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_BREAK_EXECUTION = 314,

   //
   // Breakpoints

   // Return the current list of breakpoints. These are the user requested
   // breakpoints. Resolved breakpoint locations, if any, for a requested
   // breakpoint can be obtained using RDBG_COMMAND_GET_BREAKPOINT_LOCATIONS.
   //
   //  * Presently, module name is not used and will always be a zero length
   //  string.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_bps :: uint16_t]
   // .FOR(num_bps) {
   //   [uid :: rdbg_Id]
   //   [enabled :: rdbg_Bool]
   //   [module_name :: rdbg_String]
   //   [condition_expr :: rdbg_String]
   //   [kind :: rdbg_BreakpointKind (uint8_t)]
   //   .SWITCH(kind) {
   //     .CASE(BreakpointKind_FunctionName):
   //       [function_name :: rdbg_String]
   //       [overload_id :: uint32_t]
   //     .CASE(BreakpointKind_FilenameLine):
   //       [filename :: rdbg_String]
   //       [line_num :: uint32_t]
   //     .CASE(BreakpointKind_Address):
   //       [address :: uint64_t]
   //     .CASE(BreakpointKind_Processor):
   //       [addr_expression :: rdbg_String]
   //       [num_bytes :: uint8_t]
   //       [access_kind :: rdbg_ProcessorBreakpointAccessKind (uint8_t)]
   //   }
   // }
   RDBG_COMMAND_GET_BREAKPOINTS = 600,

   // Return the list of resolved locations for a particular breakpoint. If the
   // ID is not valid for the current session RDBG_COMMAND_RESULT_INVALID_ID is
   // returned.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_locs :: uint16_t]
   // .FOR(num_locs) {
   //   [address :: uint64_t]
   //   [module_name :: rdbg_String]
   //   [filename :: rdbg_String]
   //   [actual_line_num :: uint32_t]
   // }
   RDBG_COMMAND_GET_BREAKPOINT_LOCATIONS = 601,

   // Return a list of function overloads for the given function name. If the
   // target is being debugged (suspended or executing) then returns a list of
   // function overloads for the given function name, otherwise
   // RDBG_COMMAND_RESULT_INVALID_TARGET_STATE is returned. Note that,
   // presently, all modules are searched for the given function.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [function_name :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_overloads :: uint8_t]
   // .FOR(num_overloads) {
   //   [overload_id :: rdbg_Id]
   //   [signature :: rdbg_String]
   // }
   RDBG_COMMAND_GET_FUNCTION_OVERLOADS = 602,

   // Request a breakpoint at the given function name and overload. Pass an
   // overload ID of zero to add requested breakpoints for all functions with
   // the given name.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [function_name :: rdbg_String]
   // [overload_id :: rdbg_Id]
   // [condition_expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_COMMAND_ADD_BREAKPOINT_AT_FUNCTION = 603,

   // Request a breakpoint at the given source file and line number.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [filename :: rdbg_String]
   // [line_num :: uint32_t]
   // [condition_expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_COMMAND_ADD_BREAKPOINT_AT_FILENAME_LINE = 604,

   // Request a breakpoint at the given address.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [address :: uint64_t]
   // [condition_expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_COMMAND_ADD_BREAKPOINT_AT_ADDRESS = 605,

   // Add a processor (hardware) breakpoint.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [addr_expression :: rdbg_String]
   // [num_bytes :: uint8_t]
   // [access_kind :: rdbg_ProcessorBreakpointAccessKind (uint8_t)]
   // [condition_expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_COMMAND_ADD_PROCESSOR_BREAKPOINT = 606,

   // Sets the conditional expression for the given breakpoint. Can pass in a
   // zero-length string for none.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // [condition_expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_SET_BREAKPOINT_CONDITION = 607,

   // Given an existing breakpoint of type RDBG_BREAKPOINT_KIND_FILENAME_LINE,
   // update its line number to the given one-based value.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // [line_num :: uint32_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_UPDATE_BREAKPOINT_LINE = 608,

   // Enable or disable an existing breakpoint.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // [enable :: rdbg_Bool]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_ENABLE_BREAKPOINT = 609,

   // Delete an existing breakpoint.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_BREAKPOINT = 610,

   // Delete all existing breakpoints.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_ALL_BREAKPOINTS = 611,

   // Return information about a specific user requested breakpoint.
   //
   //  * Presently, module name is not used and will always be a zero length
   //  string.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [bp_id :: rdbg_Id]
   // =>
   // [uid :: rdbg_Id]
   // [enabled :: rdbg_Bool]
   // [module_name :: rdbg_String]
   // [condition_expr :: rdbg_String]
   // [kind :: rdbg_BreakpointKind (uint8_t)]
   // .SWITCH(kind) {
   //   .CASE(BreakpointKind_FunctionName):
   //     [function_name :: rdbg_String]
   //     [overload_id :: uint32_t]
   //   .CASE(BreakpointKind_FilenameLine):
   //     [filename :: rdbg_String]
   //     [line_num :: uint32_t]
   //   .CASE(BreakpointKind_Address):
   //     [address :: uint64_t]
   //   .CASE(BreakpointKind_Processor):
   //     [addr_expression :: rdbg_String]
   //     [num_bytes :: uint8_t]
   //     [access_kind :: rdbg_ProcessorBreakpointAccessKind (uint8_t)]
   // }
   RDBG_COMMAND_GET_BREAKPOINT = 612,

   //
   // Watch Window Expressions

   // Return a list of watch expressions for the given, one-based watch window,
   // presently ranging in [1,8].
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [window_num :: uint8_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [num_watches :: uint16_t]
   // .FOR(num_watches) {
   //   [uid :: rdbg_Id]
   //   [expr :: rdbg_String]
   //   [comment :: rdbg_String]
   // }
   RDBG_COMMAND_GET_WATCHES = 700,

   // Add a watch expresion to the given, one-based watch window. Presently,
   // only single line comments are supported. Spaces will replace any newlines
   // found in a comment.
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [window_num :: uint8_t]
   // [expr :: rdbg_String]
   // [comment :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   // [uid :: rdbg_Id]
   RDBG_COMMAND_ADD_WATCH = 701,

   // Updates the expression for a given watch
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [uid :: rdbg_Id]
   // [expr :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_UPDATE_WATCH_EXPRESSION = 702,

   // Updates the comment for a given watch
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [uid :: rdbg_Id]
   // [comment :: rdbg_String]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_UPDATE_WATCH_COMMENT = 703,

   // Delete the given watch
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [uid :: rdbg_Id]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_WATCH = 704,

   // Delete all watches in the given watch window
   //
   // [cmd :: rdbg_Command (uint16_t)]
   // [window_num :: uint8_t]
   // ->
   // [result :: rdbg_CommandResult (uint16_t)]
   RDBG_COMMAND_DELETE_ALL_WATCHES = 705,
};

enum rdbg_SourceLocChangedReason
{
   RDBG_SOURCE_LOC_CHANGED_REASON_UNSPECIFIED = 0,

   // An open-file from the command-line updated the source location
   RDBG_SOURCE_LOC_CHANGED_REASON_BY_COMMAND_LINE = 1,

   // A RDBG_COMMAND_GOTO_FILE_AT_LINE from a named-pipes driver updated the source location
   RDBG_SOURCE_LOC_CHANGED_REASON_BY_DRIVER = 2,

   // A selection of a breakpoint in breakpoints pane updated the source location
   RDBG_SOURCE_LOC_CHANGED_REASON_BREAKPOINT_SELECTED = 3,

   // The current stack frame was changed in the callstack pane and updated the source location
   RDBG_SOURCE_LOC_CHANGED_REASON_CURRENT_FRAME_CHANGED = 4,

   // The active thread was changed in the threads pane and updated the source location
   RDBG_SOURCE_LOC_CHANGED_REASON_ACTIVE_THREAD_CHANGED = 5,

   //
   // The process was suspended and updated the source location
   //
   RDBG_SOURCE_LOC_CHANGED_REASON_BREAKPOINT_HIT = 6,
   RDBG_SOURCE_LOC_CHANGED_REASON_EXCEPTION_HIT = 7,
   RDBG_SOURCE_LOC_CHANGED_REASON_STEP_OVER = 8,
   RDBG_SOURCE_LOC_CHANGED_REASON_STEP_IN = 9,
   RDBG_SOURCE_LOC_CHANGED_REASON_STEP_OUT = 10,
   RDBG_SOURCE_LOC_CHANGED_REASON_NON_USER_BREAKPOINT = 11,
   RDBG_SOURCE_LOC_CHANGED_REASON_DEBUG_BREAK = 12,
};

enum rdbg_DebugEventKind
{
   // A target being debugged has exited.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [exit_code :: uint32_t]
   RDBG_DEBUG_EVENT_KIND_EXIT_PROCESS = 100,

   // The target for the active configuration is now being debugged.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [process_id :: uint32_t]
   RDBG_DEBUG_EVENT_KIND_TARGET_STARTED = 101,

   // The debugger has attached to a target process.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [process_id :: uint32_t]
   RDBG_DEBUG_EVENT_KIND_TARGET_ATTACHED = 102,

   // The debugger has detached from a target process.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [process_id :: uint32_t]
   RDBG_DEBUG_EVENT_KIND_TARGET_DETACHED = 103,

   // The debugger has transitioned from suspended to executing.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [process_id :: uint32_t]
   RDBG_DEBUG_EVENT_KIND_TARGET_CONTINUED = 104,

   // The source location changed due to an event in the debugger.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [filename :: rdbg_String]
   // [line_num :: uint32_t]
   // [reason :: rdbg_SourceLocChangedReason (uint16_t) ]
   RDBG_DEBUG_EVENT_KIND_SOURCE_LOCATION_CHANGED = 200,

   // A user breakpoint was hit
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_DEBUG_EVENT_KIND_BREAKPOINT_HIT = 600,

   // The breakpoint with the given ID has been resolved (has a valid location).
   // This can happen if the breakpoint was set in module that became loaded,
   // for instance.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_DEBUG_EVENT_KIND_BREAKPOINT_RESOLVED = 601,

   // A new user breakpoint was added.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_DEBUG_EVENT_KIND_BREAKPOINT_ADDED = 602,

   // A user breakpoint was modified.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_DEBUG_EVENT_KIND_BREAKPOINT_MODIFIED = 603,

   // A user breakpoint was removed.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [bp_id :: rdbg_Id]
   RDBG_DEBUG_EVENT_KIND_BREAKPOINT_REMOVED = 604,

   // An OutputDebugString was received by the debugger. The given string will
   // be UTF-8 encoded.
   //
   // [kind :: rdbg_DebugEventKind (uint16_t)]
   // [str :: rdbg_String]
   RDBG_DEBUG_EVENT_KIND_OUTPUT_DEBUG_STRING = 800,
};