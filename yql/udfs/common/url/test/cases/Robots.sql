/* syntax version 1 */
SELECT
    is_allowed,
    COUNT(*) AS count
FROM Input
GROUP BY Url::IsAllowedByRobotsTxt(Host, Robots, 1) AS is_allowed;
