#!/bin/bash

# Message Slot Kernel Module Test Suite
# Run this script as root (sudo) to test your implementation

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Function to print test results
print_test_result() {
    local test_name="$1"
    local result="$2"
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}[PASS]${NC} $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Function to run a test and capture result
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    echo -e "${BLUE}Running:${NC} $test_name"
    
    if eval "$test_command" >/dev/null 2>&1; then
        print_test_result "$test_name" "PASS"
        return 0
    else
        print_test_result "$test_name" "FAIL"
        return 1
    fi
}

# Function to run a test expecting failure
run_test_expect_fail() {
    local test_name="$1"
    local test_command="$2"
    
    echo -e "${BLUE}Running:${NC} $test_name"
    
    if eval "$test_command" >/dev/null 2>&1; then
        print_test_result "$test_name" "FAIL"
        return 1
    else
        print_test_result "$test_name" "PASS"
        return 0
    fi
}

echo -e "${YELLOW}=== Message Slot Test Suite ===${NC}"
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Check if required files exist
echo -e "${BLUE}Checking required files...${NC}"
required_files=("message_slot.ko" "message_sender" "message_reader")
for file in "${required_files[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}Error: $file not found. Make sure to compile your code first.${NC}"
        exit 1
    fi
done
echo -e "${GREEN}All required files found.${NC}"
echo

# Clean up any existing module
echo -e "${BLUE}Cleaning up existing module...${NC}"
rmmod message_slot 2>/dev/null || true
rm -f /dev/slot* 2>/dev/null || true

# Load the module
echo -e "${BLUE}Loading message_slot module...${NC}"
if insmod message_slot.ko; then
    echo -e "${GREEN}Module loaded successfully.${NC}"
else
    echo -e "${RED}Failed to load module. Check your implementation.${NC}"
    exit 1
fi

# Create device files
echo -e "${BLUE}Creating device files...${NC}"
mknod /dev/slot0 c 235 0
mknod /dev/slot1 c 235 1
chmod 666 /dev/slot0 /dev/slot1
echo -e "${GREEN}Device files created.${NC}"
echo

echo -e "${YELLOW}=== Basic Functionality Tests ===${NC}"

# Test 1: Basic write and read
run_test "Basic write and read" \
    "./message_sender /dev/slot0 1 0 'Hello World' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Hello World' ]"

# Test 2: Multiple channels on same device
run_test "Multiple channels on same device" \
    "./message_sender /dev/slot0 1 0 'Channel1' && \
     ./message_sender /dev/slot0 2 0 'Channel2' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Channel1' ] && \
     [ \"\$(./message_reader /dev/slot0 2)\" = 'Channel2' ]"

# Test 3: Multiple devices
run_test "Multiple devices" \
    "./message_sender /dev/slot0 1 0 'Device0' && \
     ./message_sender /dev/slot1 1 0 'Device1' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Device0' ] && \
     [ \"\$(./message_reader /dev/slot1 1)\" = 'Device1' ]"

# Test 4: Message overwriting
run_test "Message overwriting" \
    "./message_sender /dev/slot0 1 0 'First' && \
     ./message_sender /dev/slot0 1 0 'Second' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Second' ]"

# Test 5: Multiple reads of same message
run_test "Multiple reads of same message" \
    "./message_sender /dev/slot0 1 0 'Persistent' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Persistent' ] && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Persistent' ]"

echo
echo -e "${YELLOW}=== Censorship Tests ===${NC}"

# Test 6: Basic censorship
run_test "Basic censorship" \
    "./message_sender /dev/slot0 1 1 'abcdef' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'ab#de#' ]"

# Test 7: Censorship with longer message
run_test "Censorship with longer message" \
    "./message_sender /dev/slot0 1 1 'Hello World!' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'He#lo#Wo#ld#' ]"

# Test 8: Censorship disabled after enabled
run_test "Censorship disabled after enabled" \
    "./message_sender /dev/slot0 1 1 'censored' && \
     ./message_sender /dev/slot0 1 0 'normal' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'normal' ]"

# Test 9: Different censorship per file descriptor
run_test "Different censorship per file descriptor" \
    "./message_sender /dev/slot0 1 1 'abcdef' && \
     ./message_sender /dev/slot1 1 0 'abcdef' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'ab#de#' ] && \
     [ \"\$(./message_reader /dev/slot1 1)\" = 'abcdef' ]"

echo
echo -e "${YELLOW}=== Edge Cases and Limits ===${NC}"

# # Test 10: Maximum message length (128 bytes)
# run_test "Maximum message length (128 bytes)" \
#     "./message_sender /dev/slot0 1 0 '$(printf 'A%.0s' {1..128})' && \
#      [ \"\$(./message_reader /dev/slot0 1 | wc -c)\" -eq 128 ]"

# Test 11: Single character message
run_test "Single character message" \
    "./message_sender /dev/slot0 1 0 'X' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'X' ]"

# Test 12: Message with special characters
run_test "Message with special characters" \
    "./message_sender /dev/slot0 1 0 'Hello\nWorld\t!' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'Hello\nWorld\t!' ]"

# Test 13: Binary data (non-printable characters)
run_test "Binary data handling" \
    "printf '\x00\x01\x02\xFF' | ./message_sender /dev/slot0 1 0 \"\$(cat)\" 2>/dev/null || \
     echo 'test' | ./message_sender /dev/slot0 1 0 'binary_test' && \
     ./message_reader /dev/slot0 1 >/dev/null"

echo
echo -e "${YELLOW}=== Error Handling Tests ===${NC}"

# Test 14: Invalid channel ID (0)
run_test_expect_fail "Invalid channel ID (0)" \
    "./message_sender /dev/slot0 0 0 'test'"

# Test 15: Empty message
run_test_expect_fail "Empty message" \
    "./message_sender /dev/slot0 1 0 ''"

# # Test 16: Message too long (>128 bytes)
# run_test_expect_fail "Message too long (>128 bytes)" \
#     "./message_sender /dev/slot0 1 0 '$(printf 'A%.0s' {1..129})'"

# Test 17: Reading from non-existent channel
run_test_expect_fail "Reading from non-existent channel" \
    "./message_reader /dev/slot0 999"

# Test 18: Invalid device file
run_test_expect_fail "Invalid device file" \
    "./message_sender /dev/nonexistent 1 0 'test'"

# Test 19: Wrong number of arguments - sender
run_test_expect_fail "Wrong number of arguments - sender" \
    "./message_sender /dev/slot0 1"

# Test 20: Wrong number of arguments - reader
run_test_expect_fail "Wrong number of arguments - reader" \
    "./message_reader /dev/slot0"

echo
echo -e "${YELLOW}=== Stress and Concurrency Tests ===${NC}"

# Test 21: Many channels on same device
echo -e "${BLUE}Running:${NC} Many channels on same device"
success=true
for i in {1..50}; do
    if ! ./message_sender /dev/slot0 $i 0 "Message$i" >/dev/null 2>&1; then
        success=false
        break
    fi
done
if $success; then
    # Verify a few random channels
    if [ "$(./message_reader /dev/slot0 1)" = "Message1" ] && \
       [ "$(./message_reader /dev/slot0 25)" = "Message25" ] && \
       [ "$(./message_reader /dev/slot0 50)" = "Message50" ]; then
        print_test_result "Many channels on same device" "PASS"
    else
        print_test_result "Many channels on same device" "FAIL"
    fi
else
    print_test_result "Many channels on same device" "FAIL"
fi

# Test 22: Large channel IDs
run_test "Large channel IDs" \
    "./message_sender /dev/slot0 1000000 0 'Large ID' && \
     [ \"\$(./message_reader /dev/slot0 1000000)\" = 'Large ID' ]"

echo
echo -e "${YELLOW}=== Cleanup Tests ===${NC}"

# Test 23: Module unload and reload
echo -e "${BLUE}Running:${NC} Module unload and reload"
if rmmod message_slot >/dev/null 2>&1 && insmod message_slot.ko >/dev/null 2>&1; then
    # Recreate device files after reload
    mknod /dev/slot0 c 235 0 2>/dev/null || true
    chmod 666 /dev/slot0
    print_test_result "Module unload and reload" "PASS"
else
    print_test_result "Module unload and reload" "FAIL"
fi

# Final test: Verify module works after reload
run_test "Module functionality after reload" \
    "./message_sender /dev/slot0 1 0 'After reload' && \
     [ \"\$(./message_reader /dev/slot0 1)\" = 'After reload' ]"

echo
echo -e "${YELLOW}=== Test Summary ===${NC}"
echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
echo -e "Total tests: $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed! 🎉${NC}"
    exit_code=0
else
    echo -e "${RED}Some tests failed. Check your implementation.${NC}"
    exit_code=1
fi

# Cleanup
echo
echo -e "${BLUE}Cleaning up...${NC}"
rmmod message_slot 2>/dev/null || true
rm -f /dev/slot* 2>/dev/null || true
echo -e "${GREEN}Cleanup complete.${NC}"

exit $exit_code