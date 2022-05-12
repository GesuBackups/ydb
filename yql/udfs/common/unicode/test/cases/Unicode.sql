/* syntax version 1 */
SELECT
    value AS value,
    Unicode::Fold(value,
                  false AS FillOffset,
                  "Turkish" AS Language) AS fold,
    Unicode::Translit(value) AS translit
FROM (SELECT CAST(value AS Utf8) AS value FROM Input);
