import { auth } from './firebase'

const API_URL = import.meta.env.VITE_API_URL

async function request(path, options = {}) {
  const headers = { 'Content-Type': 'application/json', ...(options.headers || {}) }

  if (auth.currentUser) {
    const token = await auth.currentUser.getIdToken()
    headers['Authorization'] = `Bearer ${token}`
  }

  const res = await fetch(`${API_URL}${path}`, { ...options, headers })
  const data = await res.json().catch(() => ({}))

  if (!res.ok) {
    const err = new Error(data.error || `Request failed (${res.status})`)
    err.status = res.status
    err.data = data
    throw err
  }
  return data
}

export const api = {
  // Auth
  getMe: () => request('/me'),

  // Products
  listProducts: () => request('/products'),
  getProduct: (id) => request(`/products/${id}`),
  createProduct: (product) => request('/products', { method: 'POST', body: JSON.stringify(product) }),
  updateProduct: (id, product) => request(`/products/${id}`, { method: 'PUT', body: JSON.stringify(product) }),
  deleteProduct: (id) => request(`/products/${id}`, { method: 'DELETE' }),

  // Cart
  getCart: () => request('/cart'),
  addToCart: (productId, quantity = 1) =>
    request('/cart/add', { method: 'POST', body: JSON.stringify({ productId, quantity }) }),
  updateCartItem: (productId, quantity) =>
    request('/cart/update', { method: 'POST', body: JSON.stringify({ productId, quantity }) }),
  removeFromCart: (productId) =>
    request('/cart/remove', { method: 'POST', body: JSON.stringify({ productId }) }),

  // Orders
  createOrder: () => request('/orders', { method: 'POST' }),
  getMyOrders: () => request('/orders'),

  // Admin
  getAllOrders: () => request('/admin/orders'),
  updateOrderStatus: (id, status) =>
    request(`/admin/orders/${id}/status`, { method: 'PUT', body: JSON.stringify({ status }) }),
  listUsers: () => request('/admin/users'),
  setUserRole: (uid, role) =>
    request(`/admin/users/${uid}/role`, { method: 'PUT', body: JSON.stringify({ role }) }),
}
