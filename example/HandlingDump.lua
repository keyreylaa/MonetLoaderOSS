-- ============================================================================
-- HandlingDump.lua - Tool verifikasi offset untuk SENA Samp (arm64 saja)
-- ============================================================================
-- Fungsi: membantu menemukan layout handling data & field fisika kendaraan di
-- memori libGTASA.so, supaya script utama (Smooth Drive) bisa dibangun tanpa
-- menebak satu pun offset.
--
-- KEAMANAN: tool ini TIDAK menulis apa pun ke memori game. Semua operasi
-- hanya membaca, dibungkus pcall, dan daerah RW yang dibaca diketahui dari
-- dump resmi (gtasa-64-offsets.txt). Tidak ada pattern scan ke code page.
--
-- Yang dihasilkan di folder script (getWorkingDirectory()):
--   HandlingManager.bin   - 50724 byte RW mod_HandlingManager (base+0xCA6844)
--   Vehicle_diam.bin      - 1024 byte struct CVehicle saat kendaraan DIAM
--   Vehicle_jalan.bin     - 1024 byte struct CVehicle saat kendaraan JALAN
--   Handling_diam.bin     - 224 byte tHandlingData kendaraan saat DIAM
--   Handling_jalan.bin    - 224 byte tHandlingData kendaraan saat JALAN
--   PoolPointers.txt      - base, pointer pool, hasil pencarian pointer handling
--
-- Cara pakai:
--   1. Salin file ini ke folder scripts MonetLoader, masuk game, tunggu
--      pesan chat "[HandlingDump] Handling manager ter-dump".
--   2. Naik kendaraan terserah (rekomendasi: mobil standar, mis. WILLOW),
--      biarkan diam, ketik /dump64 diam
--   3. Berkandang LURUS & stabil (tanpa rem/belok) sekitar 60-80 km/h,
--      ketik /dump64 jalan
--   4. Kirim 4 file di atas + file handling.cfg dari APK client
--      (biasanya assets/handling.cfg di dalam com.senasamp.game.apk)
--      untuk dianalisis.
--
-- PERBAIKAN (v1.1.0):
--   - Pointer kendaraan diambil dari CUserDisplay::CurrentVehicle (global RW
--     milik game) dan bukan lagi dari pemanggilan FFI ke CPools::GetVehicle.
--     Id kendaraan dari opcode script bukan indeks pool biasa, jadi pemanggilan
--     itu bisa salah membaca atau membuat game crash. CPools::GetVehicle
--     tetap dipakai sebagai cadangan.
--   - Semua pekerjaan dijalankan di loop utama (thread game). Perintah chat
--     hanya menandai permintaan, jadi tidak ada operasi memori dari jalur
--     RPC/UI.
--   - Pointer ke tHandlingData dicari sendiri: seluruh pointer di dalam
--     struct CVehicle yang menunjuk ke mod_HandlingManager diperiksa. Jadi
--     script ini langsung memberi tahu offset pointer handling dan men-dump
--     data handling kendaraan, tanpa perlu menebak struktur.
-- ============================================================================

script_name('HandlingDump - Verifikasi Offset SENA')
script_author('olltaterfx')
script_version('1.1.0')
script_description('Dump read-only handling manager & struct CVehicle untuk verifikasi offset.')

local ffi = require 'ffi'
local memory = require 'memory'

-- Arsitektur: tool ini hanya valid untuk build arm64 (dump milikmu)
local IS_64 = ffi.sizeof('void*') == 8

-- Offset dari gtasa-64-offsets.txt (SENA Samp, arm64, dump 2026-11-12).
-- Semua RELATIF terhadap MONET_GTASA_BASE. Tidak dipakai untuk menulis;
-- hanya untuk membaca region RW yang memang valid.
local OFF_MOD_HANDLING_MGR = 0xCA6844
local MOD_HANDLING_MGR_SIZE = 50724
local OFF_GETVEHICLE_CODE  = 0x4F23F4   -- CPools::GetVehicle(int) (code, RO)
local OFF_CURRENT_VEHICLE  = 0xBDC728   -- CUserDisplay::CurrentVehicle (8 byte, RW)
local OFF_MS_PVEHICLEPOOL  = 0xBC3BC0   -- CPools::ms_pVehiclePool (8 byte, RW)
local HANDLING_DATA_SIZE   = 0xE0       -- sizeof(tHandlingData) pada manager
local VEHICLE_DUMP_SIZE    = 1024

local function baseAddr()
    local ok, n = pcall(function()
        return tonumber(ffi.cast('uintptr_t', ffi.cast('void*', MONET_GTASA_BASE)))
    end)
    return ok and n or nil
end

local function chat(msg)
    if type(sampAddChatMessage) == 'function' then
        pcall(sampAddChatMessage, '[HandlingDump] ' .. msg, -1)
    else
        print('[HandlingDump] ' .. msg)
    end
end

local function outPath(name)
    local ok, dir = pcall(getWorkingDirectory)
    if ok and type(dir) == 'string' and #dir > 0 then
        return dir .. '/' .. name
    end
    return name
end

-- Tulis biner ke file (via io, aman dari FFI)
local function writeBin(path, data)
    local f, err = io.open(path, 'wb')
    if not f then return false, err end
    f:write(data)
    f:close()
    return true
end

-- Baca `size` byte dari alamat `addr` (read-only, via memory lib)
local function dumpRegion(addr, size)
    local ok, data = pcall(memory.tostring, addr, size, true)
    if not ok or type(data) ~= 'string' or #data == 0 then
        return nil, data
    end
    return data
end

-- Baca pointer 8 byte dari alamat `addr`
local function readPointer(addr)
    local ok, value = pcall(memory.read, addr, 8, true)
    if not ok or type(value) ~= 'number' then
        return nil
    end
    return value
end

-- Baca angka 8 byte dari potongan data (tanpa FFI, supaya tidak perlu
-- perataan memori)
local function readU64(str, offset)
    local b1, b2, b3, b4, b5, b6, b7, b8 = str:byte(offset + 1, offset + 8)
    if not b1 then return nil end
    return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
        + b5 * 4294967296 + b6 * 1099511627776
        + b7 * 281474976710656 + b8 * 72057594037927936
end

local base = baseAddr()

-- 1) Dump handling manager (dijalankan sekali saat script start)
local function dumpHandlingManager()
    if not IS_64 then
        chat('Tool ini khusus build arm64 (64-bit). Dibatalkan.')
        return
    end
    if not base then
        chat('MONET_GTASA_BASE tidak ditemukan di loader ini. Dibatalkan.')
        return
    end

    local addr = base + OFF_MOD_HANDLING_MGR
    local data, err = dumpRegion(addr, MOD_HANDLING_MGR_SIZE)
    if not data then
        chat('Gagal membaca mod_HandlingManager: ' .. tostring(err))
        return
    end
    local okW, errW = writeBin(outPath('HandlingManager.bin'), data)
    if okW then
        chat(('Handling manager ter-dump (%d byte) -> HandlingManager.bin'):format(#data))
        chat(string.format('Alamat: base 0x%X + 0x%X = 0x%X', base, OFF_MOD_HANDLING_MGR, addr))
    else
        chat('Gagal menulis file: ' .. tostring(errW))
    end
end

-- 2) Pointer kendaraan: global CUserDisplay::CurrentVehicle. Isinya adalah
--    kendaraan yang sedang dipakai pemain, jadi tidak ada tebakan id pool.
local function vehiclePointerFromDisplay()
    if not base then return nil, 'base libGTASA tidak diketahui' end

    local ptr = readPointer(base + OFF_CURRENT_VEHICLE)
    if not ptr or ptr == 0 then
        return nil, 'CUserDisplay::CurrentVehicle kosong'
    end
    if ptr < 0x10000 then
        return nil, string.format('CurrentVehicle tidak masuk akal: 0x%X', ptr)
    end
    return ptr
end

-- 3) Cadangan: panggil CPools::GetVehicle(int) milik game. Id dari opcode
--    script bukan indeks pool biasa, jadi hanya panggilan game yang bisa
--    menerjemahkannya dengan benar.
local function vehiclePointerFromPool(carId)
    local okCast, fn = pcall(function()
        return ffi.cast('void* (*)(int32_t)', (base or 0) + OFF_GETVEHICLE_CODE)
    end)
    if not okCast or not fn then return nil, 'cast fungsi gagal' end

    local okCall, ptr = pcall(fn, tonumber(carId) or 0)
    if not okCall then return nil, 'panggilan GetVehicle gagal: ' .. tostring(ptr) end

    local okN, n = pcall(function()
        return tonumber(ffi.cast('uintptr_t', ptr))
    end)
    if not okN or not n or n == 0 then return nil, 'GetVehicle mengembalikan null (kendaraan ter-stream out?)' end
    if n < 0x1000 then return nil, 'pointer tidak masuk akal: ' .. string.format('0x%X', n) end
    return n
end

-- 4) Cari pointer tHandlingData di dalam struct CVehicle: pointer itu harus
--    menunjuk ke dalam mod_HandlingManager. Hasilnya memberi tahu offset
--    pointer handling tanpa menebak struktur.
local function findHandlingPointers(vehicleData)
    local start = base + OFF_MOD_HANDLING_MGR
    local stop = start + MOD_HANDLING_MGR_SIZE
    local found = {}

    for offset = 0, #vehicleData - 8, 8 do
        local value = readU64(vehicleData, offset)
        if value and value >= start and value < stop then
            found[#found + 1] = { offset = offset, value = value }
        end
    end
    return found
end

-- 5) Dump pointer pool kendaraan ke PoolPointers.txt
local function dumpPoolPointers(vehPtr, carId, handling)
    local poolPtr = readPointer(base + OFF_MS_PVEHICLEPOOL)
    local lines = {}
    lines[#lines + 1] = string.format('MONET_GTASA_BASE      = 0x%X', base or 0)
    lines[#lines + 1] = string.format('mod_HandlingManager    = 0x%X (size %d)', (base or 0) + OFF_MOD_HANDLING_MGR, MOD_HANDLING_MGR_SIZE)
    lines[#lines + 1] = string.format('ms_pVehiclePool (var)  = 0x%X', (base or 0) + OFF_MS_PVEHICLEPOOL)
    lines[#lines + 1] = string.format('ms_pVehiclePool (isi)  = 0x%X', tonumber(poolPtr) or 0)
    lines[#lines + 1] = string.format('GetVehicle code addr   = 0x%X', (base or 0) + OFF_GETVEHICLE_CODE)
    lines[#lines + 1] = string.format('CurrentVehicle addr    = 0x%X', (base or 0) + OFF_CURRENT_VEHICLE)
    lines[#lines + 1] = string.format('kendaraan id (script)  = %s', tostring(carId or '?'))
    lines[#lines + 1] = string.format('CVehicle* (saat ini)   = 0x%X', tonumber(vehPtr) or 0)
    if handling then
        lines[#lines + 1] = string.format('pHandlingData offset   = 0x%X', handling.offset)
        lines[#lines + 1] = string.format('tHandlingData*         = 0x%X', handling.value)
    end
    local f = io.open(outPath('PoolPointers.txt'), 'w')
    if f then
        f:write(table.concat(lines, '\n'))
        f:close()
        return true
    end
    return false
end

-- 6) Dump struct CVehicle + data handling kendaraan
local function dumpVehicle(label)
    if not IS_64 then
        chat('Tool ini khusus build arm64. Dibatalkan.')
        return
    end
    if not base then
        chat('MONET_GTASA_BASE tidak ada di loader ini. Dibatalkan.')
        return
    end

    local okPed, ped = pcall(getPlayerChar, 0)
    if not okPed or not ped or ped == 0 then
        chat('Ped tidak valid (loading?). Coba lagi sebentar.')
        return
    end

    local okIn, inCar = pcall(isCharInAnyCar, ped)
    if not okIn or inCar ~= true then
        chat('Kamu harus di dalam kendaraan untuk dump kendaraan. (ped=' .. tostring(ped) .. ')')
        return
    end

    local okCar, car = pcall(storeCarCharIsInNoSave, ped)
    if not okCar then
        car = nil
    end

    -- Pointer kendaraan: lebih dulu dari global game, cadangan dari pool.
    local vehPtr, err = vehiclePointerFromDisplay()
    local source = 'CUserDisplay::CurrentVehicle'
    if not vehPtr and car then
        vehPtr, err = vehiclePointerFromPool(car)
        source = 'CPools::GetVehicle'
    end
    if not vehPtr then
        chat('Gagal mendapatkan CVehicle*: ' .. tostring(err))
        return
    end

    local data, derr = dumpRegion(vehPtr, VEHICLE_DUMP_SIZE)
    if not data then
        chat('Gagal membaca CVehicle: ' .. tostring(derr))
        return
    end

    local fname = label == 'jalan' and 'Vehicle_jalan.bin' or 'Vehicle_diam.bin'
    local okW, errW = writeBin(outPath(fname), data)
    if not okW then
        chat('Gagal menulis ' .. fname .. ': ' .. tostring(errW))
        return
    end

    -- Pointer handling dicari dari struct yang baru saja di-dump.
    local pointers = findHandlingPointers(data)
    local handling = pointers[1]
    if handling then
        local hdata, herr = dumpRegion(handling.value, HANDLING_DATA_SIZE)
        if hdata then
            local hname = label == 'jalan' and 'Handling_jalan.bin' or 'Handling_diam.bin'
            local okH, errH = writeBin(outPath(hname), hdata)
            if okH then
                chat(string.format('tHandlingData ter-dump (%d byte) -> %s', #hdata, hname))
            else
                chat('Gagal menulis ' .. hname .. ': ' .. tostring(errH))
            end
        else
            chat('Gagal membaca tHandlingData: ' .. tostring(herr))
        end
        chat(string.format('Pointer handling: offset 0x%X di dalam CVehicle, nilai 0x%X',
            handling.offset, handling.value))
        if #pointers > 1 then
            chat(string.format('Catatan: %d pointer lain juga menunjuk ke handling manager', #pointers - 1))
        end
    else
        chat('Tidak ada pointer ke mod_HandlingManager di dalam 1024 byte pertama CVehicle.')
        chat('Offset pHandlingData belum ketemu: dump structnya untuk dianalisis.')
    end

    dumpPoolPointers(vehPtr, car, handling)
    chat(string.format('CVehicle ter-dump (%d byte) -> %s', #data, fname))
    chat(string.format('CVehicle* = 0x%X (%s) | pool/handling info -> PoolPointers.txt', vehPtr, source))
end

-- Perintah chat hanya menandai permintaan; pekerjaannya jalan di loop utama
-- (thread game), supaya tidak ada operasi memori di jalur RPC.
local pending = nil

local function perintahDump(param)
    local arg = tostring(param or ''):lower():match('%S+')
    if arg == 'diam' or arg == 'jalan' then
        pending = arg
        return
    end
    if arg == 'handle' or arg == 'status' then
        pending = 'status'
        return
    end
    chat('Perintah: /dump64 diam | /dump64 jalan | /dump64 status')
end

local registered = false
if type(sampRegisterChatCommand) == 'function' then
    local ok = pcall(sampRegisterChatCommand, 'dump64', perintahDump)
    registered = ok
end
if not registered and type(addCommand) == 'function' then
    pcall(addCommand, 'dump64', perintahDump, 'Dump offset kendaraan/handling SENA arm64')
end

--------------------------------------------------------------------------------
function main()
    if type(isSampAvailable) == 'function' then
        while not isSampAvailable() do wait(200) end
    end
    wait(2000)

    if not IS_64 then
        chat('Tool ini khusus arm64 (64-bit). Tidak menjalankan apa pun.')
        wait(-1)
        return
    end
    if not base then
        chat('MONET_GTASA_BASE tidak ada di loader ini. Tool dihentikan.')
        wait(-1)
        return
    end

    chat('Mulai dump handling manager...')
    dumpHandlingManager()
    dumpPoolPointers(nil, nil, nil)
    chat('Siap. Naik mobil: /dump64 diam, lalu /dump64 jalan saat/stable.')

    while true do
        wait(0)
        local job = pending
        if job then
            pending = nil
            if job == 'status' then
                dumpPoolPointers(nil, nil, nil)
                chat('Info disimpan ke PoolPointers.txt')
            else
                dumpVehicle(job)
            end
        end
    end
end
