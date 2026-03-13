-- Advanced Essence Metrology: Closed Caption Parser (Lua)
-- Decodes EIA-608/708 from SEI user_data_registered_itu_t_t35

function on_ts_section(pid, table_id, payload)
    -- table_id 0x06 is our placeholder for SEI NALU
    if table_id ~= 0x06 then return end

    local len = string.len(payload)
    if len < 8 then return end

    -- Check for ATSC 'GA94' marker
    for i = 1, math.min(len - 4, 128) do
        if string.sub(payload, i, i+3) == "GA94" then
            local type = string.byte(payload, i + 4)
            if type == 0x03 then -- cc_data()
                tsa.log(string.format("[Lua CC] Found EIA-608/708 captions on PID 0x%04X", pid))
                -- Further parsing of cc_count and cc_data_1/2 could happen here
                return
            end
        end
    end
end

tsa.log("Closed Caption Lua Parser loaded successfully.")