/*
 * Summary: Unicode character range checking
 * Description: this module exports interfaces for the character
 *              range validation APIs
 *
 * This file is automatically generated from the cvs source
 * definition files using the genChRanges.py Python script
 *
 * Generation date: Mon Mar 27 11:09:48 2006
 * Sources: chvalid.def
 */

#ifndef __XML_CHVALID_H__
#define __XML_CHVALID_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Define our typedefs and structures
 *
 */
typedef struct _xmlChSRange xmlChSRange;
typedef xmlChSRange *xmlChSRangePtr;
struct _xmlChSRange {
    unsigned short	low;
    unsigned short	high;
};

typedef struct _xmlChLRange xmlChLRange;
typedef xmlChLRange *xmlChLRangePtr;
struct _xmlChLRange {
    unsigned int	low;
    unsigned int	high;
};

typedef struct _xmlChRangeGroup xmlChRangeGroup;
typedef xmlChRangeGroup *xmlChRangeGroupPtr;
struct _xmlChRangeGroup {
    int			nbShortRange;
    int			nbLongRange;
    const xmlChSRange	*shortRange;	/* points to an array of ranges */
    const xmlChLRange	*longRange;
};



/**
 * Range checking routine
 */
int xmlCharInRange(unsigned int val, const xmlChRangeGroup *group);

int xmlIsDigit (int c);
int xmlIsLetter (int c);
int xmlIsCombining (int c);
int xmlIsExtender (int c);
int xmlIsChar (int c);

#ifdef __cplusplus
}
#endif
#endif /* __XML_CHVALID_H__ */