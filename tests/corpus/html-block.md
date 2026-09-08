# Release notes

Some prose before the raw block.

<div class="callout" data-level="warn">
  <p>Qt's Markdown writer <em>drops</em> this block entirely.</p>
  <ul><li>one</li><li>two</li></ul>
</div>

And a server-side snippet that must not be reflowed:

```php
<?php
$rows = $db->query("SELECT * FROM notes WHERE title LIKE '%*%'");
foreach ($rows as $row) { echo $row['title'] . "\n"; }
```

<?php echo "a bare processing instruction outside a fence"; ?>

Trailing prose.
