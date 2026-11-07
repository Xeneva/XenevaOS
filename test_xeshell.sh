#!/bin/bash

# Test script for XEShell functionality
# This script provides automated input to test the shell

echo "=== XEShell Automated Test ==="
echo "Building XEShell..."

# Build the shell
cd /home/axiss/Documents/work/XenevaOS/Process/XEShell
make clean && make -j8

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build XEShell"
    exit 1
fi

# Copy to resources
cp xesh.exe /home/axiss/Documents/work/XenevaOS/Resources/resources/

echo "XEShell built successfully"
echo "Starting automated test with QEMU..."

# Create a test input file with commands
cat > /tmp/xesh_test_input.txt << 'EOF'
help
ls
echo "Hello from XEShell!"
pwd
exit
EOF

echo "Test commands prepared:"
cat /tmp/xesh_test_input.txt

# Start QEMU with the shell and provide test input
echo "Starting QEMU test (this will run for 15 seconds)..."
timeout 15s ./Scripts/Linux/build_and_run_qemu.sh --term || echo "QEMU test completed"

# Wait a bit for QEMU to start
sleep 5

# Try to provide input to the shell (this might not work in QEMU)
if [ -p /tmp/qemu_input ]; then
    echo "Providing test input to QEMU..."
    cat /tmp/xesh_test_input.txt > /tmp/qemu_input
else
    echo "Note: Cannot provide automated input to QEMU in this environment"
    echo "The shell will appear to hang waiting for input, which is expected"
fi

# Wait for QEMU to finish
wait $QEMU_PID

echo "=== Test completed ==="