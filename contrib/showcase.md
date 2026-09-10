# Omanotes

Plain Markdown in the buffer, styled over the source.

## Text

**Bold**, *italic*, ***both***, ~~struck~~, `inline code`,
and a [link](https://github.com/cristian-fleischer/omanotes).

## Lists

- dash item
* asterisk item, drawn with a bullet
+ plus item
1. ordered item
   - nested under it

- [x] a task with a list marker
- [ ] one still to do
[ ] a bare checkbox needs no marker at all

## Table

| Month    | Savings  | Note                    |
|----------|----------|-------------------------|
| January  | **$250** | opening balance         |
| February | $80      | `Ctrl+Shift+T` lines up |
| March    | **$420** | _the pipes stay put_    |

## Code

```php
function total(array $rows): float {
    $sum = 0.0;
    foreach ($rows as $row) $sum += $row['amount'];
    return $sum;
}
```

```
┌──────────┐      ┌──────────┐
│  buffer  │─────▶│ styling  │
└──────────┘      └──────────┘
```

Saving writes those bytes back unchanged.

> A quote sits one shade back from the page.

---
