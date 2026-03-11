-- SCTE-35 Splice Info Section Parser (Lua)
-- Decodes PID 0x1FC (or dynamic SCTE-35 PID) sections

local function bytes_to_uint32(b1, b2, b3, b4)
    return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4
end

local function bytes_to_uint16(b1, b2)
    return (b1 << 8) | b2
end

function on_ts_section(pid, table_id, payload)
    if table_id ~= 0xFC then return end

    local len = string.len(payload)
    if len < 11 then return end

    local section_length = ((string.byte(payload, 2) & 0x0F) << 8) | string.byte(payload, 3)
    if section_length + 3 > len then return end

    local protocol_version = string.byte(payload, 4)
    local encrypted_packet = (string.byte(payload, 5) & 0x80) ~= 0
    local encryption_algorithm = (string.byte(payload, 5) & 0x7E) >> 1

    -- pts_adjustment (33 bits)
    local pts_adj_high = (string.byte(payload, 5) & 0x01)
    local pts_adj_low = bytes_to_uint32(string.byte(payload, 6), string.byte(payload, 7), string.byte(payload, 8), string.byte(payload, 9))
    local pts_adjustment = (pts_adj_high << 32) | pts_adj_low

    local cw_index = string.byte(payload, 10)
    local tier = (string.byte(payload, 11) << 4) | ((string.byte(payload, 12) & 0xF0) >> 4)
    local splice_command_length = ((string.byte(payload, 12) & 0x0F) << 8) | string.byte(payload, 13)
    local splice_command_type = string.byte(payload, 14)

    local cmd_name = "Unknown"
    if splice_command_type == 0x00 then cmd_name = "Splice Null"
    elseif splice_command_type == 0x04 then cmd_name = "Splice Schedule"
    elseif splice_command_type == 0x05 then cmd_name = "Splice Insert"
    elseif splice_command_type == 0x06 then cmd_name = "Time Signal"
    elseif splice_command_type == 0x07 then cmd_name = "Bandwidth Reservation"
    elseif splice_command_type == 0xFF then cmd_name = "Private Command"
    end

    tsa.log(string.format("[Lua SCTE-35] PID 0x%04X | Cmd: %s (0x%02X) | Adj: %u", pid, cmd_name, splice_command_type, pts_adjustment))

    if splice_command_type == 0x05 then
        local event_id = bytes_to_uint32(string.byte(payload, 15), string.byte(payload, 16), string.byte(payload, 17), string.byte(payload, 18))
        local cancel = (string.byte(payload, 19) & 0x80) ~= 0
        if not cancel then
            local out_of_network = (string.byte(payload, 20) & 0x80) ~= 0
            local program_splice = (string.byte(payload, 20) & 0x40) ~= 0
            local duration_flag = (string.byte(payload, 20) & 0x20) ~= 0
            local immediate = (string.byte(payload, 20) & 0x10) ~= 0

            tsa.log(string.format("  -> EventID: 0x%08X | Out-Of-Net: %s | Program: %s | Immediate: %s",
                event_id, tostring(out_of_network), tostring(program_splice), tostring(immediate)))

            -- Note: further byte parsing depends on the flags,
            -- but this serves as a solid foundation for Lua business-layer inspection
        end
    end
end

tsa.log("SCTE-35 Lua Parser loaded successfully.")