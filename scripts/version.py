Import("env")

from datetime import datetime, timezone
import os
import re
import subprocess
import sys

def safe_git_command(command):
    """Safely execute a git command and return the result."""
    try:
        result = subprocess.check_output(command, shell=True, stderr=subprocess.DEVNULL).decode().strip()
        return result
    except subprocess.CalledProcessError:
        print(f"Warning: Failed to execute git command: {command}")
        return "unknown"

def sanitize_branch_name(branch_name):
    """Replace problematic characters in branch name for use in filenames."""
    # Keep only alphanumeric characters
    sanitized = re.sub(r'[^a-zA-Z0-9]', '', branch_name)
    return sanitized or "unknown"

# Save time
version_datetime_utc = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

# Retrieve version information from git
git_branch = safe_git_command("git rev-parse --abbrev-ref HEAD")
git_describe = safe_git_command("git describe --always --tags --long --dirty")

# Parse git describe output
match = re.match('^(v([0-9]*?)\.([0-9]*?)\.([0-9]*?)-([0-9]*?)-g)?([0-9A-Fa-f]{5,40})(-dirty)?$', git_describe)
if match:
    version_major = match.group(2) or "0"
    version_minor = match.group(3) or "0"
    version_patch = match.group(4) or "0"
    git_post = match.group(5) or "0"
    git_hash = match.group(6)
    git_dirty = "dirty" in (match.group(7) or "")
    version_string = "v" + version_major + "." + version_minor + "." + version_patch
    if int(git_post) > 0:
        version_string += "-post." + git_post
    version_string += "+" + version_datetime_utc
    version_string += ".git" + git_hash + "." + git_branch
    if git_dirty:
        version_string += ".dirty"
else:
    version_major = "0"
    version_minor = "0"
    version_patch = "0"
    git_post = "0"
    git_hash = "unknown"
    git_dirty = False
    version_string = "v" + version_major + "." + version_minor + "." + version_patch
    version_string += "+" + version_datetime_utc

# Display version
print("Building version " + version_string)

# Create gen directory if it doesn't exist
if not os.path.exists("gen"):
    try:
        os.makedirs("gen")
    except OSError as e:
        print(f"Error creating gen directory: {e}")
        sys.exit(1)

# Output version to header file
try:
    with open("gen/version.h", "w") as f:
        f.write("#ifndef VERSION_H\n#define VERSION_H\n")
        
        # Version numbers
        f.write("\n/* Version numbers */\n")
        f.write("const int k_version_major = " + version_major + ";\n")
        f.write("const int k_version_minor = " + version_minor + ";\n")
        f.write("const int k_version_patch = " + version_patch + ";\n")
        f.write("const int k_version_post = " + git_post + ";\n")
        
        # Build information
        f.write("\n/* Build information */\n")
        f.write("const char k_version_datetime_utc[] = \"" + version_datetime_utc + "\";\n")
        
        # Git information
        f.write("\n/* Git information */\n")
        f.write("const char k_version_commit[] = \"" + git_hash + "\";\n")
        f.write("const char k_version_branch[] = \"" + git_branch + "\";\n")
        f.write("const bool k_version_dirty = " + ("true" if git_dirty else "false") + ";\n")
	
        # String summary
        f.write("\n/* String summary */\n")
        f.write("const char k_version_string[] = \"" + version_string + "\";\n")
        
        f.write("\n#endif\n")
except IOError as e:
    print(f"Error writing version.h: {e}")
    sys.exit(1)

# Create a safe filename for the binary by sanitizing the branch name
safe_branch_name = sanitize_branch_name(git_branch)
safe_version_string = version_string.replace(git_branch, safe_branch_name)

# Rename output file
env.Replace(PROGNAME="slto00001-%s" % safe_version_string)
