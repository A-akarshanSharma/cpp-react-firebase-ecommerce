import { auth } from './firebase'

const API_URL = (import.meta.env.VITE_API_URL || '').replace(/\/$/, '')
export class ApiError extends Error {
  constructor(message, status = 0, data = {}) {
    super(message)
    this.status = status
    this.data = data
  }
}
async function request(
  path,
  { method = 'GET', body, protected: secured = true, idempotencyKey, rawBody } = {},
  refreshed = false,
) {
  if (!API_URL)
    throw new ApiError('The store connection is not configured. Please contact the store owner.')
  const headers = {}
  if (idempotencyKey) headers['Idempotency-Key'] = idempotencyKey
  if (body !== undefined) headers['Content-Type'] = 'application/json'
  if (rawBody) headers['Content-Type'] = 'image/png'
  const user = auth?.currentUser
  if (secured) {
    if (!user) throw new ApiError('Please sign in to continue.', 401)
    try {
      headers.Authorization = `Bearer ${await user.getIdToken(refreshed)}`
    } catch {
      window.dispatchEvent(new Event('session-invalid'))
      throw new ApiError('Your session has expired. Please sign in again.', 401)
    }
  }
  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), 20000)
  let response, text
  try {
    response = await fetch(`${API_URL}${path}`, {
      method,
      headers,
      body: rawBody || (body === undefined ? undefined : JSON.stringify(body)),
      signal: controller.signal,
    })
    text = await response.text()
  } catch {
    throw new ApiError(
      method === 'GET'
        ? 'We could not reach the store. Check your connection and try again.'
        : 'The store did not confirm this change. Refresh to check its result before trying again.',
    )
  } finally {
    clearTimeout(timeout)
  }
  if (response.status === 401 && secured && !refreshed && auth?.currentUser === user)
    return request(path, { method, body, protected: secured, idempotencyKey, rawBody }, true)
  if (response.status === 401 && secured) window.dispatchEvent(new Event('session-invalid'))
  if (response.status === 403 && secured) window.dispatchEvent(new Event('access-denied'))
  let data
  try {
    data = text ? JSON.parse(text) : null
  } catch {
    throw new ApiError(
      'The store returned an unreadable response. Please try again.',
      response.status,
    )
  }
  if (!response.ok || data?.success === false) {
    throw new ApiError(
      (response.status === 429
        ? `Too many requests. Try again in ${response.headers.get('Retry-After') || 'a few'} seconds.`
        : data?.error) ||
        {
          401: 'Please sign in again.',
          403: 'You do not have permission to do that.',
          404: 'This item could not be found.',
        }[response.status] ||
        'The store could not complete your request.',
      response.status,
      data,
    )
  }
  return data
}
const idPath = encodeURIComponent
const array = async (promise, check) => {
  const data = await promise
  if (!Array.isArray(data) || !data.every(check))
    throw new ApiError('The store returned incomplete data. Please try again.')
  return data
}
const page = async (path, cursor, check) => {
  const params = new URLSearchParams({ limit: '25' })
  if (cursor) params.set('cursor', cursor)
  const result = await request(`${path}?${params}`)
  if (
    !result ||
    !Array.isArray(result.items) ||
    !result.items.every((item) => item && typeof item.id === 'string' && check(item)) ||
    !(
      result.nextCursor === null ||
      (typeof result.nextCursor === 'string' && result.nextCursor.length > 0)
    )
  )
    throw new ApiError('The store returned an incomplete page. Please refresh and try again.')
  return result
}
const product = (p) =>
  p &&
  typeof p.id === 'string' &&
  typeof p.name === 'string' &&
  Number.isFinite(p.price) &&
  Number.isInteger(p.stock) &&
  ['description', 'imageUrl', 'category'].every((k) => typeof p[k] === 'string')
const order = (o) =>
  o &&
  typeof o.id === 'string' &&
  typeof o.userId === 'string' &&
  typeof o.status === 'string' &&
  Number.isFinite(o.total) &&
  Number.isFinite(o.createdAt) &&
  Array.isArray(o.items) &&
  o.items.every(
    (i) => typeof i.name === 'string' && Number.isFinite(i.price) && Number.isInteger(i.quantity),
  )
export const ORDER_STATUSES = ['pending', 'paid', 'shipped', 'delivered', 'cancelled', 'expired'] // Validated by the backend; these are labels, not payment confirmation.
export const orderTransitions = {
  pending: ['paid', 'cancelled'],
  paid: ['shipped', 'cancelled'],
  shipped: ['delivered'],
  delivered: [],
  cancelled: [],
  expired: [],
}
export const api = {
  getMe: async () => {
    const p = await request('/me')
    if (
      !p ||
      typeof p.uid !== 'string' ||
      typeof p.email !== 'string' ||
      typeof p.isAdmin !== 'boolean'
    )
      throw new ApiError('Your account information could not be read.')
    return p
  },
  listProducts: () => array(request('/products', { protected: false }), product),
  getProduct: async (id) => {
    const p = await request(`/products/${idPath(id)}`, { protected: false })
    if (!product(p)) throw new ApiError('This product information could not be read.')
    return p
  },
  createProduct: (body) => request('/products', { method: 'POST', body }),
  updateProduct: (id, body) => request(`/products/${idPath(id)}`, { method: 'PUT', body }),
  adjustStock: (id, body) =>
    request(`/admin/products/${idPath(id)}/stock`, { method: 'POST', body }),
  deleteProduct: (id) => request(`/products/${idPath(id)}`, { method: 'DELETE' }),
  getCart: () =>
    array(
      request('/cart'),
      (i) =>
        i &&
        typeof i.productId === 'string' &&
        Number.isInteger(i.quantity) &&
        Number.isFinite(i.price) &&
        Number.isInteger(i.stock) &&
        typeof i.available === 'boolean',
    ),
  addToCart: (productId, quantity = 1) =>
    request('/cart/add', { method: 'POST', body: { productId, quantity } }),
  updateCartItem: (productId, quantity) =>
    request('/cart/update', { method: 'POST', body: { productId, quantity } }),
  removeFromCart: (productId) => request('/cart/remove', { method: 'POST', body: { productId } }),
  createOrder: async ({
    key,
    cartVersion,
    quoteVersion,
    pickupContact,
    shippingAddress,
    shippingMethodId,
    shippingFeeMinor,
  } = {}) => {
    const o = await request('/orders', {
      method: 'POST',
      idempotencyKey: key,
      body:
        cartVersion || quoteVersion || shippingAddress
          ? {
              ...(cartVersion ? { cartVersion } : {}),
              ...(quoteVersion ? { quoteVersion } : {}),
              ...(pickupContact ? { pickupContact } : {}),
              ...(shippingAddress ? { shippingAddress } : {}),
              ...(shippingMethodId ? { shippingMethodId, shippingFeeMinor } : {}),
            }
          : undefined,
    })
    if (!order(o))
      throw new ApiError(
        'The order response was incomplete. Check your order history before trying again.',
      )
    return o
  },
  getMyOrders: (cursor) => page('/orders', cursor, order),
  getAllOrders: () => array(request('/admin/orders'), order),
  getOrder: async (id) => {
    const result = await request(`/orders/${idPath(id)}`)
    if (!order(result)) throw new ApiError('The order details could not be read.')
    return result
  },
  cancelOrder: (id, reason, expectedStatus) =>
    request(`/orders/${idPath(id)}/cancel`, { method: 'POST', body: { reason, expectedStatus } }),
  addOrderAddress: (id, body) =>
    request(`/admin/orders/${idPath(id)}/address`, { method: 'PUT', body }),
  getInventoryHistory: (cursor) =>
    page(
      '/admin/inventory',
      cursor,
      (i) => i && typeof i.productId === 'string' && Number.isInteger(i.delta),
    ),
  updateOrderStatus: (id, status, expectedStatus) =>
    request(`/admin/orders/${idPath(id)}/status`, {
      method: 'PUT',
      body: { status, ...(expectedStatus ? { expectedStatus } : {}) },
    }),
  listShipping: () =>
    array(
      request('/shipping-methods', { protected: false }),
      (m) =>
        m && typeof m.id === 'string' && Number.isInteger(m.feeMinor) && Array.isArray(m.countries),
    ),
  getShippingSettings: () => request('/admin/shipping'),
  saveShippingSettings: (body) => request('/admin/shipping', { method: 'PUT', body }),
  saveShipment: (id, body) =>
    request(`/admin/orders/${idPath(id)}/shipment`, { method: 'PUT', body }),
  getNotifications: (cursor) =>
    page(
      '/notifications',
      cursor,
      (n) => n && typeof n.id === 'string' && typeof n.message === 'string',
    ),
  readNotification: (id) => request(`/notifications/${idPath(id)}/read`, { method: 'POST' }),
  getAuditLogs: (cursor) =>
    page('/admin/audit-logs', cursor, (a) => a && typeof a.action === 'string'),
  getVariants: (id) =>
    array(request(`/products/${idPath(id)}/variants`, { protected: false }), product),
  createVariant: (id, body) =>
    request(`/admin/products/${idPath(id)}/variants`, { method: 'POST', body }),
  uploadImage: (rawBody) => request('/admin/uploads', { method: 'POST', rawBody }),
  listUsers: () =>
    array(
      request('/admin/users'),
      (u) =>
        u && typeof u.uid === 'string' && typeof u.email === 'string' && typeof u.role === 'string',
    ),
  setUserRole: (uid, role) =>
    request(`/admin/users/${idPath(uid)}/role`, { method: 'PUT', body: { role } }),
}
