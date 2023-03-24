/************************************************************************************
   Copyright (C) 2013,2023 MariaDB Corporation AB
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc., 
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA
*************************************************************************************/

#include <ma_odbc.h>

#include "ResultSetMetaData.h"

extern MARIADB_CHARSET_INFO*  DmUnicodeCs;

char* MADB_GetTableName(MADB_Stmt *Stmt)
{
  char *TableName= nullptr;
  uint32_t  i= 0, colCount= 0;
  const MYSQL_FIELD *Field= nullptr;

  if (Stmt->TableName && Stmt->TableName[0])
  {
    return Stmt->TableName;
  }
  if (!Stmt->rs)
  {
    return nullptr;
  }

  colCount= Stmt->metadata->getColumnCount();
  Field= Stmt->metadata->getFields();
  for (i=0; i < colCount; i++)
  {
    if (Field[i].org_table)
    {
      if (!TableName)
      {
        TableName= Field[i].org_table;
      }
      if (strcmp(TableName, Field[i].org_table))
      {
        MADB_SetError(&Stmt->Error, MADB_ERR_HY000, "Couldn't identify unique table name", 0);
        return nullptr;
      }
    }
  }
  if (TableName)
  {
    Stmt->TableName = _strdup(TableName);
  }
  return Stmt->TableName;
}

char *MADB_GetCatalogName(MADB_Stmt *Stmt)
{
  char *CatalogName= nullptr;
  uint32_t i= 0, colCount= 0;
  const MYSQL_FIELD* Field = nullptr;

  if (Stmt->CatalogName && Stmt->CatalogName[0])
  {
    return Stmt->CatalogName;
  }
  if (!Stmt->metadata)
  {
    return nullptr;
  }

  colCount = Stmt->metadata->getColumnCount();
  Field = Stmt->metadata->getFields();
  for (i=0; i < colCount; i++)
  {
    if (Field[i].org_table)
    {
      if (!CatalogName)
        CatalogName= Field[i].db;
      if (strcmp(CatalogName, Field[i].db))
      {
        MADB_SetError(&Stmt->Error, MADB_ERR_HY000, "Couldn't identify unique catalog name", 0);
        return nullptr;
      }
    }
  }
  if (CatalogName)
  {
    Stmt->CatalogName= _strdup(CatalogName);
  }
  return Stmt->CatalogName;
}

my_bool MADB_DynStrAppendQuoted(MADB_DynString *DynString, char *String)
{
  if (MADB_DynstrAppendMem(DynString, "`", 1) ||
    MADB_DynstrAppend(DynString, String) ||
    MADB_DynstrAppendMem(DynString, "`", 1))
  {
    return TRUE;
  }
  return FALSE;
}

my_bool MADB_DynStrUpdateSet(MADB_Stmt *Stmt, MADB_DynString *DynString)
{
  int             i, IgnoredColumns= 0;
  MADB_DescRecord *Record;

  if (MADB_DYNAPPENDCONST(DynString, " SET "))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
    return TRUE;
  }

  const MYSQL_FIELD *Field = Stmt->metadata->getFields();
  // ???? memcpy(&Stmt->Da->Apd->Header, &Stmt->Ard->Header, sizeof(MADB_Header));
  for (i=0; i < MADB_STMT_COLUMN_COUNT(Stmt); i++)
  {
    SQLLEN *IndicatorPtr= nullptr;
    Record= MADB_DescGetInternalRecord(Stmt->Ard, i, MADB_DESC_READ);
    if (Record->IndicatorPtr)
      IndicatorPtr= (SQLLEN *)GetBindOffset(Stmt->Ard, Record, Record->IndicatorPtr, Stmt->DaeRowNumber > 1 ? Stmt->DaeRowNumber-1 : 0,
                                            sizeof(SQLLEN)/*Record->OctetLength*/);
    if ((IndicatorPtr && *IndicatorPtr == SQL_COLUMN_IGNORE) || !Record->inUse)
    {
      IgnoredColumns++;
      continue;
    }
    
    if ((i - IgnoredColumns) && MADB_DYNAPPENDCONST(DynString, ","))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
    if (MADB_DynStrAppendQuoted(DynString, Field[i].org_name))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
    if (MADB_DYNAPPENDCONST(DynString, "=?"))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
  }
  if (IgnoredColumns == Stmt->metadata->getColumnCount())
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_21S02, nullptr, 0);
    return TRUE;
  }
  return FALSE;
}

my_bool MADB_DynStrInsertSet(MADB_Stmt *Stmt, MADB_DynString *DynString)
{
  MADB_DynString  ColVals;
  int             i, NeedComma= 0;
  MADB_DescRecord *Record;
  const MYSQL_FIELD *Field;

  MADB_InitDynamicString(&ColVals, "VALUES (", 32, 32);
  if (MADB_DYNAPPENDCONST(DynString, " ("))
  {
    goto dynerror;
    
    return TRUE;
  }

  Field= Stmt->metadata->getFields();
  /* We use only columns, that have been bound, and are not IGNORED
     TODO: we gave to use this column count in most, if not all, places, where it's got via API function */
  for (i= 0; i < MADB_STMT_COLUMN_COUNT(Stmt); i++)
  {
    Record= MADB_DescGetInternalRecord(Stmt->Ard, i, MADB_DESC_READ);
    if (!Record->inUse || MADB_ColumnIgnoredInAllRows(Stmt->Ard, Record) == TRUE)
    {
      continue;
    }

    if ((NeedComma) && 
        (MADB_DYNAPPENDCONST(DynString, ",") || MADB_DYNAPPENDCONST(&ColVals, ",")))
      goto dynerror;

    if (MADB_DynStrAppendQuoted(DynString, Field[i].org_name) ||
        MADB_DYNAPPENDCONST(&ColVals, "?"))
       goto dynerror;

    NeedComma= 1;
  }
  if (MADB_DYNAPPENDCONST(DynString, ") " ) ||
      MADB_DYNAPPENDCONST(&ColVals, ")") ||
      MADB_DynstrAppend(DynString, ColVals.str))
    goto dynerror;
  MADB_DynstrFree(&ColVals);
  return FALSE;
dynerror:
  MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
  MADB_DynstrFree(&ColVals);
  return TRUE;
}

my_bool MADB_DynStrGetColumns(MADB_Stmt *Stmt, MADB_DynString *DynString)
{
  if (MADB_DYNAPPENDCONST(DynString, " ("))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
    return TRUE;
  }

  unsigned int i;
  std::size_t colCount= Stmt->metadata->getColumnCount();
  const MYSQL_FIELD *Field=   Stmt->metadata->getFields();
  for (i=0; i < colCount; i++)
  {
    if (i && MADB_DYNAPPENDCONST(DynString, ", "))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
    if (MADB_DynStrAppendQuoted(DynString, Field[i].org_name))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
  }
  if (MADB_DYNAPPENDCONST(DynString, " )"))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
    return TRUE;
  }
  return FALSE;
}

my_bool MADB_DynStrGetWhere(MADB_Stmt *Stmt, MADB_DynString *DynString, char *TableName, my_bool ParameterMarkers)
{
  int UniqueCount=0, PrimaryCount= 0, TotalPrimaryCount= 0, TotalUniqueCount= 0, TotalTableFieldCount= 0;
  int i, Flag= 0, IndexArrIdx= 0;
  char *Column= nullptr, *Escaped= nullptr;
  SQLLEN StrLength;
  unsigned long EscapedLength;

  if (Stmt->UniqueIndex != nullptr)
  {
    TotalUniqueCount= Stmt->UniqueIndex[0];
    IndexArrIdx= 1;
  }
  else
  {
    for (i = 0; i < MADB_STMT_COLUMN_COUNT(Stmt); i++)
    {
      const MYSQL_FIELD* field = FetchMetadata(Stmt)->getField(i);
      if (field->flags & PRI_KEY_FLAG)
      {
        ++PrimaryCount;
      }
      if (field->flags & UNIQUE_KEY_FLAG)
      {
        ++UniqueCount;
      }
    }

    TotalTableFieldCount = MADB_KeyTypeCount(Stmt->Connection, TableName, &TotalPrimaryCount, &TotalUniqueCount);

    if (TotalTableFieldCount < 0)
    {
      /* Error. Expecting that the called function has set the error */
      return TRUE;
    }
    /* We need to use all columns, otherwise it will be difficult to map fields for Positioned Update */
    if (PrimaryCount != TotalPrimaryCount)
    {
      PrimaryCount= 0;
    }
    if (UniqueCount != TotalUniqueCount)
    {
      UniqueCount= 0;
    }

    /* if no primary or unique key is in the cursor, the cursor must contain all
       columns from table in TableName */
       /* We use unique index if we do not have primary. TODO: If there are more than one unique index - we are in trouble */
    if (PrimaryCount != 0)
    {
      Flag= PRI_KEY_FLAG;
      /* Changing meaning of TotalUniqueCount from field count in unique index to field count in *best* unique index(that can be primary as well) */
      TotalUniqueCount= PrimaryCount;
    }
    else if (UniqueCount != 0)
    {
      Flag= UNIQUE_KEY_FLAG;
      /* TotalUniqueCount is equal UniqueCount */
    }
    else if (TotalTableFieldCount != MADB_STMT_COLUMN_COUNT(Stmt))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_S1000, "Can't build index for update/delete", 0);
      return TRUE;
    }
    else
    {
      TotalUniqueCount= 0;
    }

    if (TotalUniqueCount != 0)
    {
      /* First element gets number of columns in the index */
      Stmt->UniqueIndex= static_cast<unsigned short*>(MADB_ALLOC((TotalUniqueCount + 1) * sizeof(*Stmt->UniqueIndex)));
      if (Stmt->UniqueIndex == nullptr)
      {
        goto memerror;
      }
      Stmt->UniqueIndex[0]= TotalUniqueCount;
    }
  }

  if (MADB_DYNAPPENDCONST(DynString, " WHERE 1"))
  {
    goto memerror;
  }

  /* If we already know index columns - we walk through column index values stored in Stmt->UniqueIndex, all columns otherwise */
  for (i= IndexArrIdx == 0 ? 0 : Stmt->UniqueIndex[1];
    IndexArrIdx == 0 ? i < MADB_STMT_COLUMN_COUNT(Stmt) : IndexArrIdx <= Stmt->UniqueIndex[0];
    i= IndexArrIdx == 0 ? i + 1 : Stmt->UniqueIndex[++IndexArrIdx])
  {
    const MYSQL_FIELD *field= Stmt->metadata->getField(i);

    /* If we have already index columns, or column has right flag(primary or unique) set, or there is no good index */
    if ( IndexArrIdx != 0 || field->flags & Flag || Flag == 0)
    {
      /* Storing index information */
      if (Flag != 0)
      {
        /* Complicated(aka silly) way not to introduce another variable to store arr index for writing to the arr */
        Stmt->UniqueIndex[Stmt->UniqueIndex[0] - (--TotalUniqueCount)]= i;
      }
      if (MADB_DYNAPPENDCONST(DynString, " AND ") ||
          MADB_DynStrAppendQuoted(DynString, field->org_name))
          goto memerror;
      if (ParameterMarkers)
      {
        if (MADB_DYNAPPENDCONST(DynString, "=?"))
          goto memerror;
      }
      else
      { 
        if (!SQL_SUCCEEDED(Stmt->Methods->GetData(Stmt, i+1, SQL_C_CHAR, nullptr, 0, &StrLength, TRUE)))
        {
          MADB_FREE(Column);
          return TRUE;
        }
        if (StrLength < 0)
        {
           if (MADB_DYNAPPENDCONST(DynString, " IS NULL"))
             goto memerror;
        }
        else
        {
          Column= static_cast<char*>(MADB_CALLOC(StrLength + 1));
          Stmt->Methods->GetData(Stmt,i+1, SQL_C_CHAR, Column, StrLength + 1, &StrLength, TRUE);
          Escaped = static_cast<char*>(MADB_CALLOC(2 * StrLength + 1));
          EscapedLength= mysql_real_escape_string(Stmt->Connection->mariadb, Escaped, Column, (unsigned long)StrLength);

          if (MADB_DYNAPPENDCONST(DynString, "= '") ||
            MADB_DynstrAppend(DynString, Escaped) ||//, EscapedLength) ||
            MADB_DYNAPPENDCONST(DynString, "'"))
          {
            goto memerror;
          }
          MADB_FREE(Column);
          MADB_FREE(Escaped);
        }
      }
    }
  }

  if (MADB_DYNAPPENDCONST(DynString, " LIMIT 1"))
    goto memerror;
  MADB_FREE(Column);

  return FALSE;

memerror:
  MADB_FREE(Column);
  MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);

  return TRUE;
}


my_bool MADB_DynStrGetValues(MADB_Stmt *Stmt, MADB_DynString *DynString)
{
  unsigned int i;
  if (MADB_DYNAPPENDCONST(DynString, " VALUES("))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
    return TRUE;
  }
  for (i=0; i < Stmt->metadata->getColumnCount(); i++)
  {
    if (MADB_DynstrAppend(DynString, (i) ? ",?" : "?"))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
      return TRUE;
    }
  }
  if (MADB_DYNAPPENDCONST(DynString, ")"))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY001, nullptr, 0);
    return TRUE;
  }
  return FALSE;
}

char *MADB_GetInsertStatement(MADB_Stmt *Stmt)
{
  char *StmtStr;
  size_t Length= 1024;
  char *p;
  char *TableName;
  uint32_t i, colCount;

  if (!(TableName= MADB_GetTableName(Stmt)))
  {
    return nullptr;
  }
  if (!(StmtStr= static_cast<char*>(MADB_CALLOC(1024))))
  {
    MADB_SetError(&Stmt->Error, MADB_ERR_HY013, nullptr, 0);
    return nullptr;
  }
  p= StmtStr;
  p+= _snprintf(StmtStr, 1024, "INSERT INTO `%s` (", TableName);

  const MYSQL_FIELD* Field= Stmt->metadata->getFields();
  colCount= Stmt->metadata->getColumnCount();
  for (i=0; i < colCount; i++)
  {
    if (strlen(StmtStr) > Length - NAME_LEN - 4/* comma + 2 ticks + terminating nullptr */)
    {
      Length+= 1024;
      if (!(StmtStr= static_cast<char*>(MADB_REALLOC(StmtStr, Length))))
      {
        MADB_SetError(&Stmt->Error, MADB_ERR_HY013, nullptr, 0);
        goto error;
      }
    }
    p+= _snprintf(p, Length - strlen(StmtStr), "%s`%s`", (i==0) ? "" : ",", Field[i].org_name);
  }
  p+= _snprintf(p, Length - strlen(StmtStr), ") VALUES (");

  if (strlen(StmtStr) > Length - colCount*2 - 1)/* , and ? for each column  + (- 1 comma for 1st column + closing ')')
                                                                            + terminating nullptr */
  {
    Length= strlen(StmtStr) + colCount*2 + 1;
    if (!(StmtStr= static_cast<char*>(MADB_REALLOC(StmtStr, Length))))
    {
      MADB_SetError(&Stmt->Error, MADB_ERR_HY013, nullptr, 0);
      goto error;
    }
  }

  for (i=0; i < colCount; i++)
  {
    p+= _snprintf(p, Length - strlen(StmtStr), "%s?", (i==0) ? "" : ",");
  }
  p+= _snprintf(p, Length - strlen(StmtStr), ")");
  return StmtStr;

error:
  MADB_FREE(StmtStr);
  return nullptr;
}


my_bool MADB_ValidateStmt(MADB_QUERY *Query)
{
  return Query->QueryType != MADB_QUERY_SET_NAMES;
}


char *MADB_ToLower(const char *src, char *buff, size_t buff_size)
{
  size_t i= 0;

  if (buff_size > 0)
  {
    while (*src && i < buff_size)
    {
      buff[i++]= tolower(*src++);
    }

    buff[i == buff_size ? i - 1 : i]= '\0';
  }
  return buff;
}


int InitClientCharset(Client_Charset *cc, const char * name)
{
  /* There is no legal charset names longer than 31 chars */
  char lowered[32];
  cc->cs_info= mariadb_get_charset_by_name(MADB_ToLower(name, lowered, sizeof(lowered)));

  if (cc->cs_info == nullptr)
  {
    return 1;
  }

  cc->CodePage= cc->cs_info->codepage;

  return 0;
}


void CopyClientCharset(Client_Charset * Src, Client_Charset * Dst)
{
  Dst->CodePage= Src->CodePage;
  Dst->cs_info= Src->cs_info;
}


void CloseClientCharset(Client_Charset *cc)
{
}


/* Hmmm... Length in characters is SQLLEN, octet length SQLINTEGER */
SQLLEN MbstrOctetLen(const char *str, SQLLEN *CharLen, MARIADB_CHARSET_INFO *cs)
{
  SQLLEN result= 0, inChars= *CharLen;

  if (str)
  {
    if (cs->mb_charlen == nullptr)
    {
      /* Charset uses no more than a byte per char. Result is strlen or umber of chars */
      if (*CharLen < 0)
      {
        result= (SQLLEN)strlen(str);
        *CharLen= result;
      }
      else
      {
        result= *CharLen;
      }
      return result;
    }
    else
    {
      while (inChars > 0 || (inChars < 0 && *str))
      {
        result+= cs->mb_charlen(0 + *str);
        --inChars;
        str+= cs->mb_charlen(*str);
      }
    }
  }

  if (*CharLen < 0)
  {
    *CharLen-= inChars;
  }
  return result;
}


/* Number of characters in given number of bytes */
SQLLEN MbstrCharLen(const char *str, SQLINTEGER OctetLen, MARIADB_CHARSET_INFO *cs)
{
  SQLLEN       result= 0;
  const char   *ptr= str;
  unsigned int charlen;

  if (str)
  {
    if (cs->mb_charlen == nullptr || cs->char_maxlen == 1)
    {
      return OctetLen;
    }
    while (ptr < str + OctetLen)
    {
      charlen= cs->mb_charlen((unsigned char)*ptr);
      if (charlen == 0)
      {
        /* Dirty hack to avoid dead loop - Has to be the error! */
        charlen= 1;
      }

      /* Skipping thru 0 bytes */
      while (charlen > 0 && *ptr == '\0')
      {
          --charlen;
          ++ptr;
      }

      /* Stopping if current character is terminating nullptr - charlen == 0 means all bytes of current char was 0 */
      if (charlen == 0)
      {
        return result;
      }
      /* else we increment ptr for number of left bytes */
      ptr+= charlen;
      ++result;
    }
  }

  return result;
}


/* Length of NT SQLWCHAR string in characters */
SQLINTEGER SqlwcsCharLen(SQLWCHAR *str, SQLLEN octets)
{
  SQLINTEGER result= 0;
  SQLWCHAR   *end=   octets != (SQLLEN)-1 ? str + octets/sizeof(SQLWCHAR) : (SQLWCHAR*)octets /*for simplicity - the address to be always bigger */;

  if (str)
  {
    while (str < end && *str)
    {
      str+= (DmUnicodeCs->mb_charlen(*str))/sizeof(SQLWCHAR);

      if (str > end)
      {
        break;
      }
      ++result;
    }
  }
  return result;
}


/* Length in SQLWCHAR units
   @buff_length[in] - size of the str buffer or negative number  */
SQLLEN SqlwcsLen(SQLWCHAR *str, SQLLEN buff_length)
{
  SQLINTEGER result= 0;

  if (str)
  {
    /* If buff_length is negative - we will never hit 1st condition, otherwise we hit it after last character
       of the buffer is processed */
    while ((--buff_length) != -1 && *str)
    {
      ++result;
      ++str;
    }
  }
  return result;
}

/* Length of a string with respect to specified buffer size
@buff_length[in] - size of the str buffer or negative number  */
SQLLEN SafeStrlen(SQLCHAR *str, SQLLEN buff_length)
{
  SQLINTEGER result= 0;

  if (str)
  {
    /* If buff_length is negative - we will never hit 1st condition, otherwise we hit it after last character
    of the buffer is processed */
    while ((--buff_length) != -1 && *str)
    {
      ++result;
      ++str;
    }
  }
  return result;
}