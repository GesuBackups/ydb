/* syntax version 1 */
SELECT
    normalized_url as normalized_url,
    Url::Parse(normalized_url) AS parse,
    Url::GetOwner(value) AS owner
FROM (SELECT value, Url::NormalizeWithDefaultHttpScheme(value) AS normalized_url FROM Input);