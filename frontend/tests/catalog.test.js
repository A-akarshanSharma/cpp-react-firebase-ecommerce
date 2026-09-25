import test from 'node:test'
import assert from 'node:assert/strict'
import { filterProducts } from '../src/utils/format.js'
const products = [
  {
    id: 'a',
    name: 'Tote',
    description: 'Linen bag',
    category: 'Accessories',
    price: 900,
    stock: 4,
  },
  { id: 'b', name: 'Cup', description: 'Ceramic', category: 'Home', price: 600, stock: 0 },
  { id: 'c', name: 'Lamp', description: 'Reading light', category: 'Home', price: 2000, stock: 3 },
]
test('filters intersect without mutating the catalog', () => {
  assert.deepEqual(
    filterProducts(products, new URLSearchParams('category=Home&stock=1&min=1000&max=2200')).map(
      (p) => p.id,
    ),
    ['c'],
  )
  assert.deepEqual(
    products.map((p) => p.id),
    ['a', 'b', 'c'],
  )
})
test('search covers name, description and category, ignoring case', () => {
  for (const [q, id] of [
    ['tote', 'a'],
    ['CERAMIC', 'b'],
    ['accessories', 'a'],
  ])
    assert.equal(filterProducts(products, new URLSearchParams({ q }))[0].id, id)
})
test('all supported sorting orders and empty results', () => {
  for (const [sort, ids] of [
    ['price-asc', ['b', 'a', 'c']],
    ['price-desc', ['c', 'a', 'b']],
    ['name-asc', ['b', 'c', 'a']],
    ['name-desc', ['a', 'c', 'b']],
  ])
    assert.deepEqual(
      filterProducts(products, new URLSearchParams({ sort })).map((p) => p.id),
      ids,
    )
  assert.deepEqual(filterProducts(products, new URLSearchParams('q=missing')), [])
  assert.deepEqual(filterProducts(products, new URLSearchParams('min=2500&max=100')), [])
})

test('in-stock filtering includes an available variant when the default is sold out', () => {
  const parent = { ...products[1], hasVariants: true, hasAvailableVariants: true }
  assert.deepEqual(
    filterProducts([parent], new URLSearchParams('stock=1')).map((p) => p.id),
    ['b'],
  )
  assert.equal(parent.stock, 0)
})
