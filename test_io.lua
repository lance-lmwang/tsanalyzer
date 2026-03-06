print("Testing IO Creation in Lua...")
local input = tsa.udp_input(5000)
local output = tsa.udp_output("127.0.0.1", 6000)

tsa.log("Created input on port 5000 and output to 127.0.0.1:6000")

-- Setting variables to nil and collecting garbage to trigger __gc
input = nil
output = nil
collectgarbage()
tsa.log("Garbage collection forced, objects should be destroyed gracefully.")
