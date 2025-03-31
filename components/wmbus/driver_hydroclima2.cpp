/*
 Copyright (C) 2022 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"meters_common_implementation.h"

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);
        void processContent(Telegram *t);
    };

    static bool ok = registerDriver([](DriverInfo&di)
    {
        di.setName("hydroclima2");
        di.setDefaultFields("name,id,current_consumption_hca,average_ambient_temperature_c,timestamp");
        di.setMeterType(MeterType::HeatCostAllocationMeter);
        di.addLinkMode(LinkMode::T1);
        di.addDetection(MANUFACTURER_BMP, 0x08,  0x33);
        di.usesProcessContent();
        di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return shared_ptr<Meter>(new Driver(mi, di)); });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di) : MeterCommonImplementation(mi, di)
    {
        addNumericField("current_consumption",
                        Quantity::HCA,
                        DEFAULT_PRINT_PROPERTIES,
                        "Consumption since the beginning of this year.");

        addNumericField("previous_consumption",
                        Quantity::HCA,
                        DEFAULT_PRINT_PROPERTIES,
                        "Consumption in the previous year.");
                        

                        
        addNumericField("average_ambient_temperature",
                        Quantity::Temperature,
                        DEFAULT_PRINT_PROPERTIES,
                        "Average ambient temperature since this beginning of this year.");

        addNumericField("previous_average_ambient_temperature",
                        Quantity::Temperature,
                        DEFAULT_PRINT_PROPERTIES,
                        "Average ambient temperature in the previous year.");
    }

    double toTemperature(uchar hi, uchar lo)
    {
        return ((double)((hi<<8) | lo))/100.0;
    }

    double toIndicationU(uchar hi, uchar lo)
    {
        return ((double)((hi<<8) | lo))/10.0;
    }

    string DecodeDateTime(ushort encodedDate, ushort encodedTime)
    {
        ushort dayOfYear = (ushort)(encodedDate & 0x1FF);
        ushort yearOffset = (ushort)((encodedDate >> 9) & 0x7F);
        uint year = yearOffset + 2000;
        uint month = 0;
        uint daysInMonths[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; 
        bool isLeapYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        if(isLeapYear)
            daysInMonths[1] = 29;
        while (dayOfYear > daysInMonths[month])
        {
            dayOfYear -= daysInMonths[month];
            month++;
        }
        
        uint hour = encodedTime / 1800;
        uint minutes = (encodedTime - (hour * 1800)) / 30;
        uint seconds = (encodedTime - (hour * 1800) - (minutes * 30)) / 2;
        
        char buf[30];
        std::snprintf(buf, sizeof(buf), "%d-%02d-%02dT%02d:%02d:%02dZ", year, month, dayOfYear, hour, minutes, seconds);
        return buf;
    }
    
    void Driver::processContent(Telegram *t)
    {
        if (t->mfct_0f_index == -1) return; // Check that there is mfct data.

        int offset = t->header_size+t->mfct_0f_index;

        vector<uchar> bytes;
        t->extractMfctData(&bytes); // Extract raw frame data after the DIF 0x0F.

        debugPayload("(hydroclima mfct)", bytes);

        int i = 0;
        int len = bytes.size();
        string info;

        if (i+1 >= len) return;
        uint16_t num_measurements = bytes[i+1]<<8 | bytes[i];
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X num measurements %d", bytes[i], bytes[i+1], num_measurements);
        i+=2;
        
        if (i+1 >= len) return;
        uint16_t status = bytes[i+1]<<8 | bytes[i];
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X status", bytes[i], bytes[i+1], status);
        i+=2;
        

        if (i+3 >= len) return;
        uint16_t time = bytes[i+1]<<8 | bytes[i];
        uint16_t date = bytes[i+3]<<8 | bytes[i+2];
        string decodedDateTime = DecodeDateTime(date,time);
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X%02X%02X device date (%s)", bytes[i], bytes[i+1],
                                 bytes[i+2], bytes[i+3], decodedDateTime.c_str());
        i+=4;
        
        if (i+1 >= len) return;
        double indication_u = toIndicationU(bytes[i+1], bytes[i]);
        setNumericValue("previous_consumption", Unit::HCA, indication_u);
        info = renderJsonOnlyDefaultUnit("previous_consumption", Quantity::HCA);
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X (%s)", bytes[i], bytes[i+1], info.c_str());
        i+=2;
        
        if (i+1 >= len) return;
        double average_ambient_temperature_c = toTemperature(bytes[i+1], bytes[i]);
        setNumericValue("previous_average_ambient_temperature", Unit::C, average_ambient_temperature_c);
        info = renderJsonOnlyDefaultUnit("previous_average_ambient_temperature", Quantity::Temperature);
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X (%s)", bytes[i], bytes[i+1], info.c_str());
        i+=2;

        if (i+1 >= len) return;
        double indication_c = toIndicationU(bytes[i+1], bytes[i]);
        setNumericValue("current_consumption", Unit::HCA, indication_c);
        info = renderJsonOnlyDefaultUnit("current_consumption", Quantity::HCA);
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X (%s)", bytes[i], bytes[i+1], info.c_str());
        i+=2;
        
        if (i+1 >= len) return;
        double max_ambient_temperature_c = toTemperature(bytes[i+1], bytes[i]);
        setNumericValue("average_ambient_temperature", Unit::C, max_ambient_temperature_c);
        info = renderJsonOnlyDefaultUnit("average_ambient_temperature", Quantity::Temperature);
        t->addSpecialExplanation(i+offset, 2, KindOfData::CONTENT, Understanding::FULL,
                                 "*** %02X%02X (%s)", bytes[i], bytes[i+1], info.c_str());
        i+=2;
    }
}
