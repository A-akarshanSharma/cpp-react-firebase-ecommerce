export const money = (value) =>
  new Intl.NumberFormat('en-IN', {
    style: 'currency',
    currency: 'INR',
    maximumFractionDigits: 2,
  }).format(value)
export const date = (seconds) =>
  new Date(seconds * 1000).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' })
export const productLink = (id) => `/products/${encodeURIComponent(id)}`
export const categoryLink = (category) => `/shop?category=${encodeURIComponent(category)}`
export function filterProducts(products, params) {
  const query = (params.get('q') || '').trim().toLowerCase()
  const min = params.get('min'),
    max = params.get('max'),
    category = params.get('category')
  const result = products.filter(
    (p) =>
      (!query || `${p.name} ${p.description} ${p.category}`.toLowerCase().includes(query)) &&
      (!category || p.category === category) &&
      (!min || p.price >= Number(min)) &&
      (!max || p.price <= Number(max)) &&
      (params.get('stock') !== '1' || p.stock > 0 || p.hasAvailableVariants === true),
  )
  const comparators = {
    'price-asc': (a, b) => a.price - b.price,
    'price-desc': (a, b) => b.price - a.price,
    'name-asc': (a, b) => a.name.localeCompare(b.name),
    'name-desc': (a, b) => b.name.localeCompare(a.name),
  }
  return result.sort(comparators[params.get('sort')] || comparators['name-asc'])
}
