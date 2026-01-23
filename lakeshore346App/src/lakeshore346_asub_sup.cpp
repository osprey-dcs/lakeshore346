#include <aSubRecord.h>
#include <cstdlib>
#include <dbAccessDefs.h>
#include <devLib.h>
#include <devSup.h>
#include <epicsExport.h>
#include <epicsTypes.h>
#include <errlog.h>
#include <exception>
#include <menuFtype.h>
#include <registryFunction.h>

#include <asynPortClient.h>
#include <asynDriver.h>
#include <asynOctet.h>

#include <cstring>
#include <stdexcept>
#include <stdint.h>
#include <regex>
#include <string>

#include "lakeshore346_util.h"

// Max number of cards
static const size_t MAX_CARDS = 8;

// Max inputs (4 per card at most)
static const size_t MAX_INPUTS = MAX_CARDS*4;

// Max number of points in a calibration curve
static const size_t MAX_CURVE_PTS = 200;

static long temp_fanout_init(aSubRecord *prec) {
    #define CHECK_FTYPE(FT,FIELD,TYP,ERR)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect type for " #FIELD "; expected " #TYP "\n", prec->name);\
                return ERR;\
            }\
        } while(0)

    // aSub Inputs
    CHECK_FTYPE(fta,  INPA, UCHAR,  S_dev_badInpType);  // Number of inputs for card A
    CHECK_FTYPE(ftb,  INPB, UCHAR,  S_dev_badInpType);  // Number of inputs for card B
    CHECK_FTYPE(ftc,  INPC, UCHAR,  S_dev_badInpType);  // Number of inputs for card C
    CHECK_FTYPE(ftd,  INPD, UCHAR,  S_dev_badInpType);  // Number of inputs for card D
    CHECK_FTYPE(fte,  INPE, UCHAR,  S_dev_badInpType);  // Number of inputs for card E
    CHECK_FTYPE(ftf,  INPF, UCHAR,  S_dev_badInpType);  // Number of inputs for card F
    CHECK_FTYPE(ftg,  INPG, UCHAR,  S_dev_badInpType);  // Number of inputs for card G
    CHECK_FTYPE(fth,  INPH, UCHAR,  S_dev_badInpType);  // Number of inputs for card H

    CHECK_FTYPE(fti,  INPI, DOUBLE, S_dev_badInpType);  // Waveform with all the readings

    // aSub Outputs
    CHECK_FTYPE(ftva, OUTA, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card A
    CHECK_FTYPE(ftvb, OUTB, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card B
    CHECK_FTYPE(ftvc, OUTC, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card C
    CHECK_FTYPE(ftvd, OUTD, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card D
    CHECK_FTYPE(ftve, OUTE, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card E
    CHECK_FTYPE(ftvf, OUTF, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card F
    CHECK_FTYPE(ftvg, OUTG, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card G
    CHECK_FTYPE(ftvh, OUTH, DOUBLE, S_dev_badOutType);  // Parsed temperatures for card H

    #undef CHECK_FTYPE

    // aSub Inputs
    if (prec->noi < MAX_INPUTS) {
        errlogSevPrintf(
            errlogMajor,
            "%s: insufficient number of elements for INPI. Consider giving it at least 32",
            prec->name
        );
    }

    // aSub Outputs
    for (size_t i = 0; i < 8; ++i) {
        if (*((&(prec->nova))+i) < 4) {
            errlogSevPrintf(
                errlogMajor,
                "%s: outputs must have at least 4 elements",
                prec->name
            );
            return S_dev_badOutType;
        }
    }

    return S_dev_success;
}

static long temp_fanout_proc(aSubRecord *prec) {
    // aSub input: an array with all temperatures
    double *all_temps = static_cast<double *>(prec->i);
    size_t all_temps_len = static_cast<size_t>(prec->nei);

    // Return if we have nothing to extract
    if (all_temps_len == 0)
        return S_dev_success;

    // aSub input: number of inputs per card
    epicsUInt8 card_num_inps[MAX_CARDS] = {
        *static_cast<epicsUInt8*>(prec->a), *static_cast<epicsUInt8*>(prec->b),
        *static_cast<epicsUInt8*>(prec->c), *static_cast<epicsUInt8*>(prec->d),
        *static_cast<epicsUInt8*>(prec->e), *static_cast<epicsUInt8*>(prec->f),
        *static_cast<epicsUInt8*>(prec->g), *static_cast<epicsUInt8*>(prec->h),
    };

    // aSub output: extracted temperatures. Each VALx field will be an array of up to 4 readings
    double *extracted_card_temps[MAX_CARDS] = {
        static_cast<double*>(prec->vala), static_cast<double*>(prec->valb),
        static_cast<double*>(prec->valc), static_cast<double*>(prec->vald),
        static_cast<double*>(prec->vale), static_cast<double*>(prec->valf),
        static_cast<double*>(prec->valg), static_cast<double*>(prec->valh),
    };

    // aSub output: the length of the extracted temperatures for each card.
    epicsUInt32 *extracted_card_temps_len[MAX_CARDS] = {
        &prec->neva, &prec->nevb, &prec->nevc, &prec->nevd,
        &prec->neve, &prec->nevf, &prec->nevg, &prec->nevh,
    };

    // Collect expected number of inputs
    size_t total_expected_num_temps = 0;
    for (size_t i = 0; i < MAX_CARDS; ++i)
        total_expected_num_temps += card_num_inps[i];


    // Check that we got all the temperatures we expect based on card config
    if (total_expected_num_temps != all_temps_len) {
        errlogSevPrintf(
            errlogMajor,
            "%s: failed to extract temperatures. Expected %lu elements, got %lu",
            prec->name, total_expected_num_temps, all_temps_len
        );
        return S_dev_badArgument;
    }

    // Finally, set the parsed temperatures to our output arrays
    for (size_t card = 0, ti = 0; card < MAX_CARDS; ++card) {
        *extracted_card_temps_len[card] = card_num_inps[card];
        for (size_t i = 0; i < card_num_inps[card]; ++i)
            extracted_card_temps[card][i] = all_temps[ti++];
    }
    return S_dev_success;
}

static long curve_read_init(aSubRecord *prec) {

    #define CHECK_FTYPE(FT,FIELD,TYP,ERR)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect type for " #FIELD "; expected " #TYP "\n", prec->name);\
                return ERR;\
            }\
        } while(0)

    // aSub Inputs
    CHECK_FTYPE(fta,  INPA, CHAR,   S_dev_badInpType);  // asyn port name
    CHECK_FTYPE(ftb,  INPB, ULONG,  S_dev_badInpType);  // asyn port addr
    CHECK_FTYPE(ftc,  INPC, ULONG,  S_dev_badInpType);  // batch size
    CHECK_FTYPE(ftd,  INPD, CHAR,   S_dev_badInpType);  // progress bar PV
    CHECK_FTYPE(fte,  INPE, ULONG,  S_dev_badInpType);  // curve number
    CHECK_FTYPE(ftf,  INPF, ULONG,  S_dev_badInpType);  // curve num pts

    // aSub Outputs
    CHECK_FTYPE(ftva, OUTA, DOUBLE, S_dev_badOutType);  // Parsed unit values
    CHECK_FTYPE(ftvb, OUTB, DOUBLE, S_dev_badOutType);  // Parsed temperatures

    #undef CHECK_FTYPE

    // aSub Inputs
    if (prec->nob != 1 || prec->noc != 1 || prec->noe != 1) {
        errlogSevPrintf(
            errlogMajor,
            "%s: expected 1 element for NOB, NOC and NOE",
            prec->name
        );
        return S_dev_badInpType;
    }

    // aSub Outputs
    for (size_t i = 0; i < 2; ++i) {
        if (*((&(prec->nova))+i) < MAX_CURVE_PTS) {
            errlogSevPrintf(
                errlogMajor,
                "%s: outputs must have at least %lu elements",
                prec->name, MAX_CURVE_PTS
            );
            return S_dev_badOutType;
        }
    }

    return S_dev_success;
}

static long curve_read_proc(aSubRecord *prec) {
    const char *port = static_cast<char *>(prec->a);
    epicsUInt32 addr = *static_cast<epicsUInt32*>(prec->b);
    epicsUInt32 batch_size = *static_cast<epicsUInt32*>(prec->c);
    char *progress_rec = static_cast<char*>(prec->d);
    epicsUInt32 curve_num = *static_cast<epicsUInt32*>(prec->e);
    epicsUInt32 curve_numpts = *static_cast<epicsUInt32*>(prec->f);

    if (! (1 <= curve_num && curve_num <= 60)) {
        errlogSevPrintf(
            errlogMajor,
            "%s: curve number must be between 1 and 60. Got: %d\n",
            prec->name, curve_num
        );
        return S_dev_badArgument;
    }

    const std::string curve_num_str = std::to_string(curve_num);

    asynOctetClient client = asynOctetClient(port, addr, NULL);

    // Find progress record
    DBADDR progress_rec_addr;
    if (dbNameToAddr(progress_rec, &progress_rec_addr)) {
        errlogSevPrintf(
            errlogMajor,
            "%s: unable to find record '%s'\n",
            prec->name, progress_rec
        );
        return S_dev_badArgument;
    }

    dbPutField(&progress_rec_addr, DBR_STRING, "0", 1);

    try {
        size_t out_i = 0;
        const std::string crvpt_prefix = ";CRVPT? " + curve_num_str + ",";
        for (size_t i = 0; i < curve_numpts; ) {
            std::string request;
            for (size_t j = 0; (j < batch_size) && (i < curve_numpts); ++i, ++j) {
                request += crvpt_prefix + std::to_string(i+1);
            }

            std::string response = read_write(client, request);

            // Parse response
            static const std::regex PT_PAIR("([\\d\\.]+),([\\d\\.]+)");

            auto pairs_begin = std::sregex_iterator(response.begin(), response.end(), PT_PAIR);
            auto pairs_end = std::sregex_iterator();

            for (std::sregex_iterator it = pairs_begin; it != pairs_end; ++it) {
                std::smatch m = *it;

                double units_value = std::stod(m[1].str());
                double temp_value = std::stod(m[2].str());

                static_cast<epicsFloat64*>(prec->vala)[out_i] = units_value;
                static_cast<epicsFloat64*>(prec->valb)[out_i] = temp_value;
                ++out_i;
            }

            double progress = 100.0*i/curve_numpts;
            dbPutField(&progress_rec_addr, DBR_STRING, std::to_string(progress).c_str(), 1);
        }
        prec->neva = out_i;
        prec->nevb = out_i;

    } catch (const std::runtime_error & ex) {
        errlogSevPrintf( errlogMajor, "%s: curve %d: %s\n", prec->name, curve_num, ex.what());
        return S_dev_badRequest;
    }

    return S_dev_success;
}

static long curve_write_init(aSubRecord *prec) {

    #define CHECK_FTYPE(FT,FIELD,TYP,ERR)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect type for " #FIELD "; expected " #TYP "\n", prec->name);\
                return ERR;\
            }\
        } while(0)

    // aSub Inputs
    CHECK_FTYPE(fta,  INPA, CHAR,   S_dev_badInpType);  // asyn port name
    CHECK_FTYPE(ftb,  INPB, ULONG,  S_dev_badInpType);  // asyn port addr
    CHECK_FTYPE(ftc,  INPC, ULONG,  S_dev_badInpType);  // batch size
    CHECK_FTYPE(ftd,  INPD, CHAR,   S_dev_badInpType);  // progress bar PV
    CHECK_FTYPE(fte,  INPE, ULONG,  S_dev_badInpType);  // curve number
    CHECK_FTYPE(ftf,  INPF, DOUBLE, S_dev_badInpType);  // units waveform
    CHECK_FTYPE(ftg,  INPG, DOUBLE, S_dev_badInpType);  // temps waveform

    #undef CHECK_FTYPE

    // aSub Inputs
    if (prec->nob != 1 || prec->noc != 1 || prec->noe != 1) {
        errlogSevPrintf(
            errlogMajor,
            "%s: expected 1 element for NOB, NOC and NOE",
            prec->name
        );
        return S_dev_badInpType;
    }

    if (prec->nof < MAX_CURVE_PTS || prec->nog < MAX_CURVE_PTS) {
        errlogSevPrintf(
            errlogMajor,
            "%s: inputs must hold at least %lu elements",
            prec->name, MAX_CURVE_PTS
        );
        return S_dev_badOutType;
    }

    return S_dev_success;
}

static long curve_write_proc(aSubRecord *prec) {
    const char *port = static_cast<char *>(prec->a);
    epicsUInt32 addr = *static_cast<epicsUInt32*>(prec->b);
    epicsUInt32 batch_size = *static_cast<epicsUInt32*>(prec->c);
    char *progress_rec = static_cast<char*>(prec->d);
    epicsUInt32 curve_num = *static_cast<epicsUInt32*>(prec->e);
    epicsFloat64 *curve1_in = static_cast<epicsFloat64*>(prec->f);
    epicsFloat64 *curve2_in = static_cast<epicsFloat64*>(prec->g);

    if (! (21 <= curve_num && curve_num <= 60)) {
        errlogSevPrintf(
            errlogMajor,
            "%s: curve number must be between 21 and 60. Got: %d\n",
            prec->name, curve_num
        );
        return S_dev_badArgument;
    }

    if (prec->nef != prec->neg) {
        errlogSevPrintf(
            errlogMajor,
            "%s: both input curves must have the same length\n",
            prec->name
        );
        return S_dev_badArgument;
    }

    const std::string curve_num_str = std::to_string(curve_num);

    asynOctetClient client = asynOctetClient(port, addr, NULL);

    // Find progress record
    DBADDR progress_rec_addr;
    if (dbNameToAddr(progress_rec, &progress_rec_addr)) {
        errlogSevPrintf(
            errlogMajor,
            "%s: unable to find record '%s'\n",
            prec->name, progress_rec
        );
        return S_dev_badArgument;
    }

    dbPutField(&progress_rec_addr, DBR_STRING, "0", 1);

    size_t curve_numpts = prec->nef;
    bool found_zeroes = false;

    // Find first 0,0 pair
    for (size_t i = 0; i < curve_numpts; ++i) {
        if (curve1_in[i] == 0.0f && curve2_in[i] == 0.0f) {
            curve_numpts = i+1;
            found_zeroes = true;
        }
    }

    try {
        const std::string crvpt_prefix = ";CRVPT " + curve_num_str + ",";
        for (size_t i = 0; i < curve_numpts; ) {
            std::string request;
            for (size_t j = 0; (j < batch_size) && (i < curve_numpts); ++i, ++j) {
                request += crvpt_prefix + std::to_string(i+1);
                request += "," + std::to_string(curve1_in[i]);
                request += "," + std::to_string(curve2_in[i]);
            }
            request += ";*OPC?";

            std::string response = read_write(client, request);

            if (response != "1") {
                throw std::runtime_error("Failed to upload curve points");
            }

            double progress = 100.0*i/curve_numpts;
            dbPutField(&progress_rec_addr, DBR_STRING, std::to_string(progress).c_str(), 1);
        }

        if (!found_zeroes) {
            std::string request = "CRVPT " + curve_num_str + "," + std::to_string(curve_numpts+1) + ",0,0;*OPC?";
            std::string response = read_write(client, request);

            if (response != "1") {
                throw std::runtime_error("Failed to upload final curve point");
            }
        }

    } catch (const std::runtime_error & ex) {
        errlogSevPrintf( errlogMajor, "%s: curve %d: %s\n", prec->name, curve_num, ex.what());
        return S_dev_badRequest;
    }

    return S_dev_success;
}

static long curve_resize_init(aSubRecord *prec) {
    #define CHECK_FTYPE(FT,FIELD,TYP,ERR)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect type for " #FIELD "; expected " #TYP "\n", prec->name);\
                return ERR;\
            }\
        } while(0)

    // aSub Inputs
    CHECK_FTYPE(fta,  INPA, DOUBLE, S_dev_badInpType);  // input curve1
    CHECK_FTYPE(ftb,  INPB, DOUBLE, S_dev_badInpType);  // input curve2
    CHECK_FTYPE(ftc,  INPB, ULONG,  S_dev_badInpType);  // new size

    // aSub Outputs
    CHECK_FTYPE(ftva, OUTA, DOUBLE, S_dev_badOutType);  // output curve1
    CHECK_FTYPE(ftvb, OUTB, DOUBLE, S_dev_badOutType);  // output curve2

    #undef CHECK_FTYPE

    // aSub Inputs
    if (prec->noa < MAX_CURVE_PTS || prec->nob < MAX_CURVE_PTS || prec->noc != 1) {
        errlogSevPrintf(
            errlogMajor,
            "%s: expected %lu elements for NOA and NOB, 1 element for NOC\n",
            prec->name, MAX_CURVE_PTS
        );
        return S_dev_badInpType;
    }

    // aSub Outputs
    if (prec->nova < MAX_CURVE_PTS || prec->novb < MAX_CURVE_PTS) {
        errlogSevPrintf(
            errlogMajor,
            "%s: outputs must have at least %lu elements\n",
            prec->name, MAX_CURVE_PTS
        );
        return S_dev_badOutType;
    }

    return S_dev_success;
}

static long curve_resize_proc(aSubRecord *prec) {
    epicsFloat64 *curve1_in = static_cast<epicsFloat64*>(prec->a);
    epicsFloat64 *curve2_in = static_cast<epicsFloat64*>(prec->b);
    epicsUInt32 new_size = *static_cast<epicsUInt32*>(prec->c);

    epicsFloat64 *curve1_out = static_cast<epicsFloat64*>(prec->vala);
    epicsFloat64 *curve2_out = static_cast<epicsFloat64*>(prec->valb);

    if (new_size < 1 || new_size > MAX_CURVE_PTS) {
        errlogSevPrintf(
            errlogMajor,
            "%s: new size must be between 1 and %lu. Got: %d\n",
            prec->name, MAX_CURVE_PTS, new_size
        );
        return S_dev_badArgument;
    }

    if (prec->nea != prec->neb) {
        errlogSevPrintf(
            errlogMajor,
            "%s: input curves are expected to have the same length\n",
            prec->name
        );
        return S_dev_badArgument;
    }

    size_t num_elem_to_copy = std::min(prec->nea, new_size);
    size_t i = 0;

    // Copy existing elements
    for (; i < new_size; ++i) {
        curve1_out[i] = i < num_elem_to_copy ? curve1_in[i] : 0.0f;
        curve2_out[i] = i < num_elem_to_copy ? curve2_in[i] : 0.0f;
    }

    prec->neva = i;
    prec->nevb = i;

    return S_dev_success;
}


epicsRegisterFunction(temp_fanout_init);
epicsRegisterFunction(temp_fanout_proc);
epicsRegisterFunction(curve_read_init);
epicsRegisterFunction(curve_read_proc);
epicsRegisterFunction(curve_write_init);
epicsRegisterFunction(curve_write_proc);
epicsRegisterFunction(curve_resize_init);
epicsRegisterFunction(curve_resize_proc);
