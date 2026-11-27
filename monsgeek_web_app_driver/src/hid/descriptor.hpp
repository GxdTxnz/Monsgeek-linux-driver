#pragma once

// разбор описания HID (report_descriptor) (информация о клавиатуре при подключении)

#include <vector>
#include <cstdint>

// есть ли в HID-описании интерфейса тип с нужными usagePage + usage
bool hasCollection(const std::vector<uint8_t> &descriptor, uint16_t usagePage, uint16_t usage);
