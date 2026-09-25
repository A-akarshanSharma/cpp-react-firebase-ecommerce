import { test, expect } from '@playwright/test'
import { readFileSync } from 'node:fs'
const imageBytes = readFileSync(new URL('./fixtures/product.png', import.meta.url))
const shippingAddress = {
  name: 'Test Buyer',
  phone: '9876543210',
  line1: '1 Test Street',
  line2: '',
  city: 'Pune',
  region: 'Maharashtra',
  postalCode: '411001',
  country: 'IN',
}
async function fillAddress(page) {
  for (const [label, key] of [
    ['Recipient name', 'name'],
    ['Phone number', 'phone'],
    ['Address line 1', 'line1'],
    ['City', 'city'],
    ['State / region', 'region'],
    ['Postal code', 'postalCode'],
  ])
    await page.getByLabel(label, { exact: true }).fill(shippingAddress[key])
  if (new URL(page.url()).pathname === '/checkout')
    await page.getByLabel('Shipping method', { exact: true }).selectOption({ index: 1 })
}
// Test fixtures only. The running application always uses the configured backend.
const initialProducts = [
  {
    id: 'linen',
    name: 'Linen Everyday Tote',
    description: 'A thoughtful linen carryall.',
    price: 1200,
    stock: 5,
    category: 'Accessories',
    imageUrl: '',
  },
  {
    id: 'cup',
    name: 'Ceramic Cup',
    description: 'A warm morning ritual.',
    price: 650,
    stock: 3,
    category: 'Home',
    imageUrl: '/missing-image.jpg',
  },
  {
    id: 'lamp',
    name: 'Studio Lamp',
    description: 'Light for your evenings.',
    price: 2400,
    stock: 0,
    category: 'Home',
    imageUrl: '',
  },
]
async function setup(
  page,
  {
    role = 'customer',
    products = initialProducts,
    down = false,
    malformed = false,
    stockError = false,
    expired = false,
  } = {},
) {
  const state = {
    products: structuredClone(products).map((p) => ({ ...p, version: 'initial' })),
    cart: [],
    orders: [],
    inventory: [],
    shipping: {
      methods: [
        {
          id: 'fixture-delivery',
          name: 'Standard delivery',
          kind: 'delivery',
          fee: 0,
          feeMinor: 0,
          active: true,
          countries: [],
          estimatedDays: '',
        },
      ],
      version: 'initial',
    },
    notifications: [],
    audits: [],
    uploads: 0,
    users: [
      { uid: 'test-user', email: 'shopper@example.test', role },
      { uid: 'other-user', email: 'other@example.test', role: 'customer' },
    ],
    writes: [],
    cartVersion: 'cart-fixture-v1',
    meCalls: 0,
    refreshes: 0,
  }
  const token = () =>
    `${Buffer.from(JSON.stringify({ alg: 'none', typ: 'JWT' })).toString('base64url')}.${Buffer.from(JSON.stringify({ sub: 'test-user', user_id: 'test-user', email: 'shopper@example.test', aud: 'studio-test', iss: 'https://securetoken.google.com/studio-test', iat: Math.floor(Date.now() / 1000), exp: Math.floor(Date.now() / 1000) + 3600 })).toString('base64url')}.test-signature`
  await page.route('https://fonts.googleapis.com/**', (route) => route.fulfill({ body: '' }))
  await page.route('https://identitytoolkit.googleapis.com/**', async (route) => {
    const path = new URL(route.request().url()).pathname
    if (path.endsWith('accounts:lookup'))
      return route.fulfill({
        json: {
          users: [
            {
              localId: 'test-user',
              email: 'shopper@example.test',
              emailVerified: false,
              providerUserInfo: [
                {
                  providerId: 'password',
                  email: 'shopper@example.test',
                  federatedId: 'shopper@example.test',
                },
              ],
            },
          ],
        },
      })
    const body = route.request().postDataJSON()
    if (body?.password === 'wrong-password')
      return route.fulfill({
        status: 400,
        json: { error: { message: 'INVALID_LOGIN_CREDENTIALS' } },
      })
    return route.fulfill({
      json: {
        localId: 'test-user',
        email: 'shopper@example.test',
        idToken: token(),
        refreshToken: 'test-refresh-token',
        expiresIn: '3600',
      },
    })
  })
  await page.route('https://securetoken.googleapis.com/**', (route) => {
    state.refreshes++
    return route.fulfill({
      json: {
        access_token: token(),
        id_token: token(),
        refresh_token: 'test-refresh-token',
        expires_in: '3600',
        token_type: 'Bearer',
        user_id: 'test-user',
        project_id: 'studio-test',
      },
    })
  })
  await page.route('http://127.0.0.1:8089/**', async (route) => {
    const request = route.request(),
      path = new URL(request.url()).pathname,
      method = request.method()
    const reply = (json, status = 200) =>
      route.fulfill({
        status,
        json,
        headers: {
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Headers': 'Content-Type,Authorization,Idempotency-Key',
          'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS',
        },
      })
    const replyPage = (records) => {
      const params = new URL(request.url()).searchParams
      const limit = Number(params.get('limit') || 25)
      const offset = Number(params.get('cursor') || 0)
      const rows = [...records].sort(
        (a, b) => b.createdAt - a.createdAt || b.id.localeCompare(a.id),
      )
      return reply({
        items: rows.slice(offset, offset + limit),
        nextCursor: rows.length > offset + limit ? String(offset + limit) : null,
      })
    }
    if (method === 'OPTIONS') return reply({})
    if (down) return route.abort('failed')
    if (path.startsWith('/media/'))
      return route.fulfill({ contentType: 'image/png', body: imageBytes })
    if (path === '/shipping-methods') return reply(state.shipping.methods.filter((m) => m.active))
    if (path.endsWith('/variants') && method === 'GET')
      return reply(state.products.filter((p) => p.parentProductId === path.split('/')[2]))
    if (path === '/products' && method === 'GET')
      return reply(
        malformed
          ? { invalid: true }
          : state.products
              .filter((p) => !p.parentProductId)
              .map((p) => ({
                ...p,
                hasAvailableVariants: state.products.some(
                  (v) => v.parentProductId === p.id && v.stock > 0,
                ),
              })),
      )
    if (path.startsWith('/products/') && method === 'GET') {
      const p = state.products.find((p) => p.id === decodeURIComponent(path.split('/')[2]))
      return reply(p || { error: 'Product not found' }, p ? 200 : 404)
    }
    expect(request.headers().authorization).toMatch(/^Bearer /)
    if (path === '/me') {
      state.meCalls++
      return reply({
        uid: 'test-user',
        email: 'shopper@example.test',
        isAdmin: state.users[0].role === 'admin',
      })
    }
    if (expired && path === '/cart')
      return reply({ error: 'Not authenticated: expired token' }, 401)
    if (path === '/admin/uploads') {
      expect(request.headers()['content-type']).toBe('image/png')
      expect(request.postDataBuffer().subarray(1, 4).toString()).toBe('PNG')
      state.uploads++
      return reply({ id: 'a'.repeat(64), url: `/media/${'a'.repeat(64)}.png` })
    }
    const body = request.postData() ? request.postDataJSON() : undefined
    if (method !== 'GET')
      state.writes.push({ path, method, body, key: request.headers()['idempotency-key'] })
    if (path === '/admin/shipping' && method === 'GET') return reply(state.shipping)
    if (path === '/admin/shipping' && method === 'PUT') {
      state.shipping = {
        methods: body.methods.map((m) => ({ ...m, feeMinor: Math.round(m.fee * 100) })),
        version: 'v2',
      }
      return reply(state.shipping)
    }
    if (path === '/admin/audit-logs') return replyPage(state.audits)
    if (path === '/notifications') return replyPage(state.notifications)
    if (path.startsWith('/notifications/') && path.endsWith('/read')) {
      state.notifications.find((n) => n.id === path.split('/')[2]).read = true
      return reply({ success: true })
    }
    if (path.endsWith('/shipment')) {
      const order = state.orders.find((o) => o.id === path.split('/')[3])
      order.shipment = { ...body, version: 'tracking-v1' }
      return reply(order)
    }
    if (path.endsWith('/variants') && method === 'POST') {
      const parentId = path.split('/')[3]
      state.products.find((p) => p.id === parentId).hasVariants = true
      state.products.push({
        ...body,
        version: 'initial',
        id: 'variant-1',
        parentProductId: parentId,
      })
      return reply({ success: true, id: 'variant-1' })
    }
    if (path === '/cart')
      return reply(
        state.cart.map((i) => {
          const p = state.products.find((p) => p.id === i.productId)
          return {
            ...i,
            cartVersion: state.cartVersion,
            quoteVersion: `quote-${state.products.find((p) => p.id === i.productId)?.price}`,
            name: p?.name || 'No longer available',
            price: p?.price || 0,
            stock: p?.stock || 0,
            imageUrl: p?.imageUrl || '',
            available: !!p && p.stock >= i.quantity,
          }
        }),
      )
    if (path.startsWith('/cart/')) {
      expect(Object.keys(body).sort()).toEqual(
        path === '/cart/remove' ? ['productId'] : ['productId', 'quantity'],
      )
      const p = state.products.find((p) => p.id === body.productId),
        existing = state.cart.find((i) => i.productId === body.productId)
      if (stockError && path === '/cart/add')
        return reply(
          { error: 'Not enough stock available', available: 0, requested: body.quantity },
          409,
        )
      if (path === '/cart/remove')
        state.cart = state.cart.filter((i) => i.productId !== body.productId)
      else {
        const quantity = body.quantity + (path === '/cart/add' ? existing?.quantity || 0 : 0)
        if (quantity > p.stock)
          return reply(
            { error: 'Not enough stock available', available: p.stock, requested: quantity },
            409,
          )
        if (existing) existing.quantity = quantity
        else state.cart.push({ productId: body.productId, quantity })
      }
      return reply({ success: true, cart: state.cart })
    }
    if (path === '/orders' && method === 'POST') {
      expect(body.cartVersion).toBe(state.cartVersion)
      const selected = state.shipping.methods.find((m) => m.id === body.shippingMethodId)
      if (selected?.kind === 'pickup') expect(body.pickupContact).toBeTruthy()
      else expect(body.shippingAddress).toEqual(shippingAddress)
      if (selected) expect(body.shippingFeeMinor).toBe(selected.feeMinor)
      expect(request.headers()['idempotency-key']).toMatch(/^[a-f0-9-]{36}$/)
      const items = state.cart.map((i) => {
        const p = state.products.find((p) => p.id === i.productId)
        p.stock -= i.quantity
        return { ...i, name: p.name, price: p.price }
      })
      const order = {
        id: 'order-001',
        userId: 'test-user',
        items,
        total: items.reduce((s, i) => s + i.price * i.quantity, 0),
        shippingAddress: body.shippingAddress,
        pickupContact: body.pickupContact,
        expiresAt: Math.floor(Date.now() / 1000) + 14400,
        statusHistory: [{ to: 'pending', createdAt: 1789128000 }],
        status: 'pending',
        createdAt: 1789128000,
      }
      const fee = selected?.feeMinor || 0
      order.total += fee / 100
      order.shippingFeeMinor = fee
      if (selected) order.shippingMethod = selected
      state.notifications.push({
        id: 'notification-1',
        orderId: order.id,
        message: 'Your order has been placed.',
        read: false,
        createdAt: order.createdAt,
      })
      state.orders.push(order)
      state.cart = []
      return reply(order)
    }
    if (path === '/orders' && method === 'GET') return replyPage(state.orders)
    if (path === '/admin/orders' && method === 'GET') return reply(state.orders)
    if (path === '/admin/inventory') return replyPage(state.inventory)
    if (path.startsWith('/orders/') && method === 'GET') {
      const order = state.orders.find((o) => o.id === path.split('/')[2])
      return reply(order || { error: 'Order not found' }, order ? 200 : 404)
    }
    if (path.endsWith('/cancel')) {
      const order = state.orders.find((o) => o.id === path.split('/')[2])
      if (order.status !== 'pending' && !(role === 'admin' && order.status === 'paid'))
        return reply({ error: 'Order cannot be cancelled' }, 409)
      for (const item of order.items)
        state.products.find((p) => p.id === item.productId).stock += item.quantity
      order.refundStatus = order.status === 'paid' ? 'manual_review_required' : 'not_required'
      order.status = 'cancelled'
      order.statusHistory = [
        ...(order.statusHistory || []),
        { to: 'cancelled', reason: body.reason, createdAt: 1789128100 },
      ]
      return reply(order)
    }
    if (path.endsWith('/address')) {
      const order = state.orders.find((o) => o.id === path.split('/')[3])
      if (order.shippingAddress) return reply({ error: 'Address already recorded' }, 409)
      order.shippingAddress = body
      return reply(order)
    }
    if (path === '/admin/users') return reply(state.users)
    if (path.endsWith('/role')) {
      expect(Object.keys(body)).toEqual(['role'])
      const target = state.users.find((u) => u.uid === path.split('/')[3])
      if (
        target.role === 'admin' &&
        body.role === 'customer' &&
        state.users.filter((u) => u.role === 'admin').length === 1
      )
        return reply(
          {
            code: 'LAST_ADMIN',
            error:
              'Keep at least one administrator. Promote another account before removing this access.',
          },
          409,
        )
      target.role = body.role
      return reply({ success: true })
    }
    if (path.endsWith('/status')) {
      expect(Object.keys(body).sort()).toEqual(['expectedStatus', 'status'])
      state.orders.find((o) => o.id === path.split('/')[3]).status = body.status
      return reply({ success: true })
    }
    if (path === '/products' && method === 'POST') {
      expect(Object.keys(body).sort()).toEqual([
        'category',
        'description',
        'imageUrl',
        'name',
        'price',
        'stock',
      ])
      state.products.push({ ...body, id: 'new-product', version: 'initial' })
      return reply({ success: true, id: 'new-product' })
    }
    if (path.startsWith('/products/') && method === 'PUT') {
      const id = path.split('/')[2]
      expect(body).not.toHaveProperty('stock')
      const p = state.products.find((p) => p.id === id)
      if (body.version !== p.version)
        return reply(
          {
            code: 'PRODUCT_CHANGED',
            error: 'This product changed. Reload the latest product before saving.',
          },
          409,
        )
      Object.assign(p, body, { version: `${p.version}-next` })
      return reply({ success: true })
    }
    if (path.startsWith('/admin/products/') && path.endsWith('/stock')) {
      const p = state.products.find((p) => p.id === path.split('/')[3])
      if (body.version !== p.version)
        return reply(
          {
            code: 'PRODUCT_CHANGED',
            error: 'Stock or product details changed. Reload before adjusting stock.',
          },
          409,
        )
      expect(body.reason.trim()).not.toBe('')
      p.stock += body.delta
      p.version = `${p.version}-stock`
      return reply({ success: true, product: p })
    }
    if (path.startsWith('/products/') && method === 'DELETE') {
      state.products = state.products.filter((p) => p.id !== path.split('/')[2])
      return reply({ success: true })
    }
    throw new Error(`Unexpected API request: ${method} ${path}`)
  })
  return state
}
async function login(page, destination = '/account') {
  await page.goto(destination)
  await expect(page.getByRole('heading', { name: 'Good to see you again.' })).toBeVisible()
  await page.getByLabel('Email address', { exact: true }).fill('shopper@example.test')
  await page.getByLabel('Password', { exact: true }).fill('safe-password')
  await page.getByRole('button', { name: 'Sign in', exact: true }).click()
  await expect(page).toHaveURL(new RegExp(destination.replaceAll('/', '\\/') + '$'))
}

test('guest browsing, filters, details, missing images and protected routes', async ({ page }) => {
  await setup(page)
  await page.goto('/')
  await expect(page.getByRole('heading', { name: 'Less ordinary. More you.' })).toBeVisible()
  await page.getByRole('link', { name: 'Shop all', exact: true }).first().click()
  await expect(page.locator('.product-card')).toHaveCount(3)
  await page.getByLabel('Search', { exact: true }).fill('linen')
  await expect(page.locator('.product-card')).toHaveCount(1)
  await page.getByRole('button', { name: 'Clear all' }).click()
  await page.getByLabel('Category', { exact: true }).selectOption('Home')
  await page.getByLabel('In stock only').check()
  await expect(page.locator('.product-card')).toHaveCount(1)
  await page.getByLabel('Minimum price').fill('700')
  await expect(page.getByRole('heading', { name: 'No finds this time' })).toBeVisible()
  await page.getByRole('button', { name: 'Clear all' }).click()
  await page.getByLabel('Sort by').selectOption('price-desc')
  await expect(page.locator('.product-card').first()).toContainText('Studio Lamp')
  await page.getByRole('link', { name: 'View Ceramic Cup', exact: true }).click()
  await expect(
    page.getByRole('img', { name: 'Ceramic Cup: image unavailable' }).first(),
  ).toBeVisible()
  await page.getByRole('button', { name: 'Add to bag: Ceramic Cup' }).click()
  await expect(page).toHaveURL(/\/login$/)
  await page.goto('/products/missing')
  await expect(page.getByText('Product not found', { exact: true })).toBeVisible()
  for (const route of ['/cart', '/orders', '/admin/products']) {
    await page.goto(route)
    await expect(page).toHaveURL(/\/login$/)
  }
})

test('customer bag, checkout, history, session restoration and authorization', async ({ page }) => {
  const state = await setup(page)
  await login(page)
  await page.reload()
  await expect(page.getByRole('heading', { name: 'Your account', exact: true })).toBeVisible()
  await page.goto('/products/linen')
  await page.getByRole('button', { name: 'Add to bag: Linen Everyday Tote' }).click()
  await expect(page.getByLabel('Shopping bag, 1 items')).toBeVisible()
  await page.goto('/cart')
  await page.getByRole('button', { name: 'Increase quantity' }).click()
  await expect(page.getByLabel('Quantity', { exact: true })).toHaveText('2')
  await page.getByRole('button', { name: 'Decrease quantity' }).click()
  await expect(page.getByLabel('Quantity', { exact: true })).toHaveText('1')
  await page.getByRole('button', { name: 'Remove', exact: true }).click()
  await expect(
    page.getByRole('heading', { name: 'A little room for something good' }),
  ).toBeVisible()
  await page.goto('/products/cup')
  await page.getByRole('button', { name: 'Add to bag: Ceramic Cup' }).click()
  await expect(page.getByLabel('Shopping bag, 1 items')).toBeVisible()
  await page.goto('/cart')
  await page.getByRole('link', { name: 'Proceed to checkout' }).click()
  await fillAddress(page)
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  await expect(page.getByLabel('Shopping bag, 0 items')).toBeVisible()
  await page.getByRole('link', { name: 'View your orders' }).click()
  await page.locator('.order-card summary').click()
  await expect(page.getByRole('cell', { name: 'Ceramic Cup', exact: true })).toBeVisible()
  expect(state.writes.filter((w) => w.path === '/orders')).toHaveLength(1)
  await page.goto('/admin/products')
  await expect(
    page.getByRole('heading', { name: 'This area is for store administrators' }),
  ).toBeVisible()
  await expect(page.getByRole('link', { name: 'Dashboard ↗' })).toHaveCount(0)
})

test('stale product edits preserve sold stock and can reload safely', async ({ page }) => {
  const state = await setup(page, { role: 'admin' })
  state.products.find((p) => p.id === 'cup').imageUrl = ''
  await login(page, '/admin/products/cup/edit')
  await page.getByLabel('Product name').fill('Edited cup')
  const p = state.products.find((p) => p.id === 'cup')
  p.stock = 1
  p.version = 'checkout-version'
  await page.getByRole('button', { name: 'Save changes' }).click()
  await expect(page.getByRole('alert')).toContainText('This product changed')
  expect(p.stock).toBe(1)
  expect(p.name).not.toBe('Edited cup')
  await expect(page.getByLabel('Product name')).toHaveValue('Edited cup')
  await page.getByRole('button', { name: 'Reload latest product (discard edits)' }).click()
  await expect(page.getByLabel('Product name')).toHaveValue(p.name)
  await page.getByLabel('Product name').fill('Fresh edit')
  await page.getByRole('button', { name: 'Save changes' }).click()
  await expect(page.getByRole('heading', { name: 'Products', exact: true })).toBeVisible()
  expect(p.name).toBe('Fresh edit')
  expect(p.stock).toBe(1)
})

test('stock adjustment requires a fresh review after a conflict', async ({ page }) => {
  const state = await setup(page, { role: 'admin' })
  await login(page, '/admin/products/cup/edit')
  await expect(page.getByRole('heading', { name: 'Adjust stock' })).toBeVisible()
  await page.getByLabel('Stock change', { exact: true }).fill('2')
  await page.getByLabel('Adjustment reason').fill('Received shipment')
  const p = state.products.find((p) => p.id === 'cup')
  p.stock = 1
  p.version = 'checkout-version'
  await page.getByRole('button', { name: 'Apply stock adjustment' }).click()
  await expect(page.getByRole('alert')).toContainText('Reload before adjusting stock')
  await expect(page.getByRole('button', { name: 'Apply stock adjustment' })).toBeDisabled()
  expect(p.stock).toBe(1)
  await page.getByRole('button', { name: 'Reload current stock' }).click()
  await expect(page.getByText('Current stock: 1', { exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Apply stock adjustment' }).click()
  await expect(page.getByText('Current stock: 3', { exact: true })).toBeVisible()
  expect(p.stock).toBe(3)
  await expect(page.getByLabel('Stock change', { exact: true })).toHaveValue('')
})

test('last admin control is disabled and concurrent demotion rejection stays visible', async ({
  page,
}) => {
  const state = await setup(page, { role: 'admin' })
  await login(page, '/admin/users')
  await expect(page.getByLabel('Role for shopper@example.test')).toBeDisabled()
  await expect(page.getByText('Last administrator — promote another account first.')).toBeVisible()
  await page.getByLabel('Role for other@example.test').selectOption('admin')
  await page.getByRole('button', { name: 'Change role', exact: true }).click()
  await expect(page.getByLabel('Role for shopper@example.test')).toBeEnabled()
  await page.getByLabel('Role for shopper@example.test').selectOption('customer')
  state.users.find((u) => u.uid === 'other-user').role = 'customer'
  await page.getByRole('button', { name: 'Change role', exact: true }).click()
  await expect(page.getByRole('dialog').getByRole('alert')).toContainText(
    'Keep at least one administrator',
  )
  expect(state.users.find((u) => u.uid === 'test-user').role).toBe('admin')
})

test('admin product CRUD, order status, role changes and own demotion', async ({ page }) => {
  const state = await setup(page, { role: 'admin' })
  state.orders.push({
    id: 'existing-order',
    userId: 'other-user',
    status: 'pending',
    createdAt: 1789128000,
    total: 650,
    items: [{ productId: 'cup', name: 'Ceramic Cup', price: 650, quantity: 1 }],
  })
  await login(page, '/admin')
  await expect(page.getByRole('heading', { name: 'A little overview.' })).toBeVisible()
  await page.getByRole('link', { name: 'Products', exact: true }).click()
  await page.getByRole('link', { name: '+ Add product' }).click()
  await page.getByLabel('Product name').fill('Test Basket')
  await page.getByLabel('Description', { exact: true }).fill('A test-only basket.')
  await page.getByLabel('Category', { exact: true }).fill('Home')
  await page.getByLabel('Price (₹)', { exact: true }).fill('900')
  await page.getByLabel('Stock quantity').fill('4')
  await page.getByRole('button', { name: 'Create product' }).click()
  await expect(page.getByRole('row').filter({ hasText: 'Test Basket' })).toBeVisible()
  await page
    .getByRole('row')
    .filter({ hasText: 'Test Basket' })
    .getByRole('link', { name: 'Edit', exact: true })
    .click()
  await expect(page.getByLabel('Price (₹)', { exact: true })).toHaveValue('900')
  await page.getByLabel('Price (₹)', { exact: true }).fill('950')
  await page.getByRole('button', { name: 'Save changes' }).click()
  const row = page.getByRole('row').filter({ hasText: 'Test Basket' })
  await expect(row).toContainText('950')
  await row.getByRole('button', { name: 'Delete', exact: true }).click()
  await expect(page.getByRole('dialog')).toBeVisible()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  await expect(row).toBeVisible()
  await row.getByRole('button', { name: 'Delete', exact: true }).click()
  await page.getByRole('button', { name: 'Delete product', exact: true }).click()
  await expect(row).toHaveCount(0)
  await page.getByRole('link', { name: 'Orders', exact: true }).click()
  await page.getByLabel('Status for order existing-order').selectOption('paid')
  await page.getByRole('button', { name: 'Update status', exact: true }).click()
  await expect(page.getByLabel('Status for order existing-order')).toHaveValue('paid')
  await page.getByRole('button', { name: 'View', exact: true }).click()
  await expect(page.getByRole('cell', { name: 'Ceramic Cup', exact: true })).toBeVisible()
  await page.getByRole('link', { name: 'Users', exact: true }).click()
  await page.getByLabel('Role for other@example.test').selectOption('admin')
  await page.getByRole('button', { name: 'Change role', exact: true }).click()
  await expect(page.getByLabel('Role for other@example.test')).toHaveValue('admin')
  await page.getByLabel('Role for shopper@example.test').selectOption('customer')
  await expect(page.getByText('You will lose your own admin access immediately.')).toBeVisible()
  await page.getByRole('button', { name: 'Change role', exact: true }).click()
  await expect(
    page.getByRole('heading', { name: 'This area is for store administrators' }),
  ).toBeVisible()
})

test('registration and invalid credentials', async ({ page }) => {
  await setup(page)
  await page.goto('/login')
  await page.getByLabel('Email address', { exact: true }).fill('shopper@example.test')
  await page.getByLabel('Password', { exact: true }).fill('wrong-password')
  await page.getByRole('button', { name: 'Sign in', exact: true }).click()
  await expect(page.getByText('The email or password is incorrect.')).toBeVisible()
  await page.getByRole('link', { name: 'Create an account' }).click()
  await page.getByLabel('Email address', { exact: true }).fill('shopper@example.test')
  await page.getByLabel('Password', { exact: true }).fill('safe-password')
  await page.getByLabel('Confirm password').fill('mismatch')
  await page.getByRole('button', { name: 'Create account' }).click()
  await expect(page.getByText('Your passwords do not match.')).toBeVisible()
  await page.getByLabel('Confirm password').fill('safe-password')
  await page.getByRole('button', { name: 'Create account' }).click()
  await expect(page).toHaveURL(/\/account$/)
})

test('stock conflict, empty orders, expired session', async ({ page }) => {
  await setup(page, { stockError: true })
  await login(page, '/orders')
  await expect(page.getByRole('heading', { name: 'Your first find is waiting' })).toBeVisible()
  await page.goto('/products/cup')
  await page.getByRole('button', { name: 'Add to bag: Ceramic Cup' }).click()
  await expect(page.locator('.toast.error')).toContainText('Available stock: 0')
  await page.goto('/products/lamp')
  await expect(page.getByRole('button', { name: 'Out of stock: Studio Lamp' })).toBeDisabled()
})

test('expired token retries once and blocks protected content', async ({ page }) => {
  const state = await setup(page, { expired: true })
  await login(page)
  await expect(
    page.getByText('Your session could not be verified. Sign out and sign in again.'),
  ).toBeVisible()
  expect(state.refreshes).toBeGreaterThan(0)
  await expect(page.getByRole('heading', { name: 'Your account', exact: true })).toHaveCount(0)
})

for (const [label, options, message] of [
  ['offline', { down: true }, 'We could not reach the store.'],
  ['empty', { products: [] }, 'The collection is coming together'],
  ['malformed', { malformed: true }, 'The store returned incomplete data.'],
]) {
  test(`${label} catalog has an understandable state`, async ({ page }) => {
    await setup(page, options)
    await page.goto('/shop')
    await expect(page.getByText(message, { exact: false })).toBeVisible()
  })
}
for (const width of [390, 768, 1440]) {
  test(`responsive storefront and dashboard at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 1000 })
    const state = await setup(page, { role: 'admin' })
    state.cart = [{ productId: 'cup', quantity: 1 }]
    state.orders = [
      {
        id: 'responsive-order',
        userId: 'test-user',
        shippingAddress,
        statusHistory: [{ to: 'pending', createdAt: 1789128000 }],
        status: 'pending',
        createdAt: 1789128000,
        total: 650,
        items: [{ productId: 'cup', name: 'Ceramic Cup', price: 650, quantity: 1 }],
      },
    ]
    const errors = []
    page.on('pageerror', (e) => errors.push(e.message))
    for (const route of ['/', '/shop', '/products/cup', '/login']) {
      await page.goto(route)
      await expect(page.locator('h1,h2').first()).toBeVisible()
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(
        true,
      )
      if (route === '/')
        await page.screenshot({ path: `test-results/home-${width}.png`, fullPage: true })
    }
    await login(page, '/admin')
    for (const route of [
      '/admin',
      '/admin/products',
      '/admin/products/new',
      '/admin/orders',
      '/admin/inventory',
      '/admin/shipping',
      '/admin/audit',
      '/notifications',
      '/orders/responsive-order',
      '/admin/users',
      '/cart',
      '/checkout',
      '/orders',
      '/account',
    ]) {
      await page.goto(route)
      await expect(page.locator('h1,h2').first()).toBeVisible()
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(
        true,
      )
    }
    await page.goto('/checkout')
    await fillAddress(page)
    await page.screenshot({ path: `test-results/checkout-${width}.png`, fullPage: true })
    await page.goto('/admin')
    await expect(page.getByRole('heading', { name: 'A little overview.' })).toBeVisible()
    await page.screenshot({ path: `test-results/admin-${width}.png`, fullPage: true })
    expect(errors).toEqual([])
  })
}

test('ambiguous order failure prevents an immediate duplicate submission', async ({ page }) => {
  const state = await setup(page)
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  let submissions = 0
  const attempts = []
  await page.route('http://127.0.0.1:8089/orders', async (route) => {
    if (route.request().method() === 'POST') {
      submissions++
      attempts.push({
        key: route.request().headers()['idempotency-key'],
        body: route.request().postDataJSON(),
      })
      return route.fulfill({ status: 500, json: { error: 'Order result could not be confirmed' } })
    }
    return route.fallback()
  })
  await fillAddress(page)
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('link', { name: 'check your order history' })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Confirm order' })).toBeDisabled()
  expect(submissions).toBe(1)
  state.cartVersion = 'cart-fixture-v2'
  await page.reload()
  await fillAddress(page)
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('link', { name: 'check your order history' })).toBeVisible()
  expect(submissions).toBe(2)
  expect(attempts[0].key).toMatch(/^[a-f0-9-]{36}$/)
  expect(attempts[0].body).toEqual({
    cartVersion: 'cart-fixture-v1',
    quoteVersion: `quote-${state.products.find((p) => p.id === 'cup').price}`,
    shippingAddress,
    shippingMethodId: 'fixture-delivery',
    shippingFeeMinor: 0,
  })
  expect(attempts[1]).toEqual(attempts[0])
})

test('backend 403 revokes cached admin access', async ({ page }) => {
  await setup(page, { role: 'admin' })
  await login(page, '/admin')
  await expect(page.getByRole('heading', { name: 'A little overview.' })).toBeVisible()
  await page.route('http://127.0.0.1:8089/admin/users', (route) =>
    route.fulfill({ status: 403, json: { error: 'Admin access required' } }),
  )
  await page.getByRole('link', { name: 'Users', exact: true }).click()
  await expect(
    page.getByRole('heading', { name: 'This area is for store administrators' }),
  ).toBeVisible()
})

test('admin confirmation traps focus, cancels with Escape, and preserves edits on focus', async ({
  page,
}) => {
  await setup(page, { role: 'admin' })
  await login(page, '/admin/products')
  await page.getByRole('button', { name: 'Delete', exact: true }).first().click()
  await expect(page.getByRole('dialog')).toBeFocused()
  await page.keyboard.press('Tab')
  await expect(page.getByRole('button', { name: 'Cancel', exact: true })).toBeFocused()
  await page.keyboard.press('Shift+Tab')
  await expect(page.getByRole('button', { name: 'Delete product', exact: true })).toBeFocused()
  await page.keyboard.press('Escape')
  await expect(page.getByRole('dialog')).toHaveCount(0)
  await page.getByRole('link', { name: 'Edit', exact: true }).first().click()
  await page.getByLabel('Product name').fill('Unsaved draft')
  await page.evaluate(() => window.dispatchEvent(new Event('focus')))
  await expect(page.getByLabel('Product name')).toHaveValue('Unsaved draft')
})

test('order detail shows address and customer cancellation updates stock', async ({ page }) => {
  const state = await setup(page)
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  await page.getByLabel('Shipping method', { exact: true }).selectOption({ index: 1 })
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  expect(state.orders).toHaveLength(0)
  await fillAddress(page)
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  expect(state.products.find((p) => p.id === 'cup').stock).toBe(2)
  await page.goto('/orders/order-001')
  await expect(page.getByText('1 Test Street', { exact: false })).toBeVisible()
  await page.getByRole('button', { name: 'Cancel order', exact: true }).click()
  await page.getByLabel('Cancellation reason (optional)').fill('Changed plans')
  await page.getByRole('button', { name: 'Confirm cancellation', exact: true }).click()
  await expect(page.locator('.status')).toHaveText('cancelled')
  await expect(page.getByRole('button', { name: 'Cancel order', exact: true })).toHaveCount(0)
  expect(state.products.find((p) => p.id === 'cup').stock).toBe(3)
  await page.reload()
  await expect(page.locator('.status')).toHaveText('cancelled')
  await page.goto('/orders/missing')
  await expect(page.getByText('Order not found', { exact: true })).toBeVisible()
})

test('admin records legacy address, fulfills order and inspects inventory', async ({ page }) => {
  const state = await setup(page, { role: 'admin' })
  state.orders.push({
    id: 'legacy-order',
    userId: 'other-user',
    status: 'paid',
    total: 650,
    createdAt: 1789128000,
    items: [{ productId: 'cup', name: 'Ceramic Cup', price: 650, quantity: 1 }],
  })
  state.inventory.push({
    id: 'movement-1',
    productId: 'cup',
    delta: -1,
    stockBefore: 3,
    stockAfter: 2,
    reason: 'order_placed',
    actorId: 'other-user',
    orderId: 'legacy-order',
    createdAt: 1789128000,
  })
  await login(page, '/orders/legacy-order')
  await expect(page.getByRole('button', { name: 'Mark shipped' })).toBeDisabled()
  await fillAddress(page)
  await page.getByRole('button', { name: 'Save order address' }).click()
  await expect(page.getByRole('button', { name: 'Mark shipped' })).toBeEnabled()
  await page.getByRole('button', { name: 'Mark shipped' }).click()
  await page.getByRole('button', { name: 'Confirm status' }).click()
  await expect(page.locator('.status')).toHaveText('shipped')
  await expect(page.getByRole('button', { name: 'Cancel order', exact: true })).toHaveCount(0)
  await page.getByRole('button', { name: 'Mark delivered' }).click()
  await page.getByRole('button', { name: 'Confirm status' }).click()
  await expect(page.locator('.status')).toHaveText('delivered')
  await page.goto('/admin/inventory')
  await expect(page.getByRole('heading', { name: 'Inventory history' })).toBeVisible()
  await expect(page.getByRole('cell', { name: 'order placed' })).toBeVisible()
  await page.getByLabel('Search inventory history').fill('no-such-product')
  await expect(page.getByRole('heading', { name: 'No stock movements found' })).toBeVisible()
})

test('store operations: admin shipping configuration, upload, variants and audit view', async ({
  page,
}) => {
  const state = await setup(page, { role: 'admin' })
  state.shipping.methods = []
  state.audits = [
    {
      id: 'audit-1',
      actorId: 'test-user',
      action: 'product_updated',
      targetId: 'cup',
      changes: { stock: 3 },
      createdAt: 1789128000,
    },
  ]
  await login(page, '/admin/shipping')
  await page.getByRole('button', { name: 'Add shipping method' }).click()
  await page.getByLabel('Name', { exact: true }).fill('Standard')
  await page.getByLabel('Delivery fee (₹)').fill('50')
  await page.getByLabel('Country codes (comma separated)').fill('IN')
  await page.getByLabel('Estimated delivery').fill('3–5 days')
  await page.getByRole('button', { name: 'Save shipping settings' }).click()
  await expect(page.getByText('Shipping settings saved.')).toBeVisible()
  expect(state.shipping.methods[0].feeMinor).toBe(5000)
  await page.goto('/admin/products/cup/edit')
  await page.getByLabel('Upload product image').setInputFiles('tests/fixtures/product.png')
  await expect(page.getByLabel('Image URL')).toHaveValue(`/media/${'a'.repeat(64)}.png`)
  await page.getByRole('button', { name: 'Save changes' }).click()
  await expect(page.getByRole('heading', { name: 'Products', exact: true })).toBeVisible()
  expect(state.uploads).toBe(1)
  await page.goto('/admin/products/cup/edit')
  await page.getByLabel('Variant option').fill('Blue / Large')
  await page.getByLabel('Variant SKU').fill('CUP-BLUE-L')
  await page.getByLabel('Variant price (₹)').fill('700')
  await page.getByLabel('Variant stock').fill('2')
  await page.getByRole('button', { name: 'Add variant', exact: true }).click()
  await expect(page.getByRole('link', { name: 'Edit variant' })).toBeVisible()
  await page.getByRole('link', { name: 'Edit variant' }).click()
  await expect(page.getByText('Current stock: 2', { exact: true })).toBeVisible()
  await page.getByRole('link', { name: 'Back to parent product' }).click()
  await page.getByRole('button', { name: 'Remove variant', exact: true }).click()
  await page
    .getByRole('dialog')
    .getByRole('button', { name: 'Remove variant', exact: true })
    .click()
  await expect(page.getByRole('link', { name: 'Edit variant' })).toHaveCount(0)
  await page.goto('/admin/audit')
  await expect(page.getByRole('cell', { name: 'product updated' })).toBeVisible()
  await page.getByLabel('Search audit log').fill('missing')
  await expect(page.getByRole('heading', { name: 'No audit records found' })).toBeVisible()
})

test('variant checkout includes shipping and customer notifications can be read', async ({
  page,
}) => {
  const state = await setup(page)
  state.products.find((p) => p.id === 'cup').hasVariants = true
  state.products.push({
    ...state.products[1],
    id: 'blue-cup',
    name: 'Ceramic Cup — Blue',
    variantLabel: 'Blue',
    sku: 'CUP-BLUE',
    parentProductId: 'cup',
    price: 700,
    stock: 2,
  })
  state.shipping.methods = [
    {
      id: 'standard',
      name: 'Standard',
      fee: 50,
      feeMinor: 5000,
      countries: ['IN'],
      active: true,
      estimatedDays: '3–5 days',
    },
  ]
  await login(page)
  await page.goto('/products/cup')
  await page.getByLabel('Choose an option').selectOption('blue-cup')
  await page.getByRole('button', { name: 'Add to bag: Ceramic Cup — Blue' }).click()
  await expect(page.getByLabel('Shopping bag, 1 items')).toBeVisible()
  await page.goto('/checkout')
  await fillAddress(page)
  await page.getByLabel('Shipping method').selectOption('standard')
  await expect(page.locator('.summary-total')).toContainText('750')
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  expect(state.orders[0].total).toBe(750)
  expect(state.products.find((p) => p.id === 'cup').stock).toBe(3)
  expect(state.products.find((p) => p.id === 'blue-cup').stock).toBe(1)
  await page.goto('/notifications')
  await expect(page.getByText('Your order has been placed.', { exact: false })).toBeVisible()
  await page.getByRole('button', { name: 'Mark read' }).click()
  await expect(page.getByRole('button', { name: 'Mark read' })).toHaveCount(0)
  expect(state.notifications[0].read).toBe(true)
})

test('staff tracking entry is visible on order details', async ({ page }) => {
  const state = await setup(page, { role: 'admin' })
  state.orders = [
    {
      id: 'tracking-order',
      userId: 'other-user',
      status: 'paid',
      total: 650,
      createdAt: 1789128000,
      shippingAddress,
      items: [{ productId: 'cup', name: 'Ceramic Cup', price: 650, quantity: 1 }],
    },
  ]
  await login(page, '/orders/tracking-order')
  await page.getByLabel('Carrier', { exact: true }).fill('Test Carrier')
  await page.getByLabel('Tracking number', { exact: true }).fill('TRACK-123')
  await page.getByLabel('Tracking URL (optional)').fill('https://example.test/track/123')
  await page.getByRole('button', { name: 'Save tracking' }).click()
  await expect(page.getByRole('link', { name: 'Track shipment' })).toHaveAttribute(
    'href',
    'https://example.test/track/123',
  )
  await expect(page.getByText('Test Carrier · TRACK-123')).toBeVisible()
})

test('checkout refreshes a changed price and requires fresh confirmation', async ({ page }) => {
  const state = await setup(page)
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  await expect(page.getByRole('heading', { name: 'Checkout', exact: true })).toBeVisible()
  await fillAddress(page)
  await expect(page.locator('.checkout-address')).toContainText('1 Test Street')
  await page.getByLabel('I have reviewed my items and total.').check()
  const attempts = []
  await page.route('http://127.0.0.1:8089/orders', async (route) => {
    if (route.request().method() !== 'POST') return route.fallback()
    attempts.push({
      key: route.request().headers()['idempotency-key'],
      body: route.request().postDataJSON(),
    })
    if (attempts.length === 1) {
      state.products.find((p) => p.id === 'cup').price = 900
      return route.fulfill({
        status: 409,
        json: {
          code: 'PRICE_CHANGED',
          error: 'Prices changed. Review the updated items and total, then confirm again.',
        },
      })
    }
    return route.fallback()
  })
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('alert')).toContainText('Prices changed')
  await expect(page.locator('.summary-total')).toContainText('900')
  await expect(page.getByLabel('I have reviewed my items and total.')).not.toBeChecked()
  await expect(page.getByRole('button', { name: 'Confirm order' })).toBeDisabled()
  expect(state.orders).toHaveLength(0)
  await expect(page.getByLabel('Address line 1', { exact: true })).toHaveValue('1 Test Street')
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  expect(attempts[1].key).not.toBe(attempts[0].key)
  expect(attempts[1].body.quoteVersion).toBe('quote-900')
  expect(state.orders[0].total).toBe(900)
})

test('checkout is disabled when staff have no active methods', async ({ page }) => {
  const state = await setup(page)
  state.shipping.methods = []
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  await expect(
    page.getByText('Checkout is temporarily unavailable. Please contact the store.'),
  ).toBeVisible()
  await page.getByLabel('I have reviewed my items and total.').check()
  await expect(page.getByRole('button', { name: 'Confirm order' })).toBeDisabled()
  expect(state.orders).toHaveLength(0)
})

test('pickup checkout requires contact and records pickup instructions', async ({ page }) => {
  const state = await setup(page)
  state.shipping.methods = [
    {
      id: 'pickup',
      name: 'Store pickup',
      kind: 'pickup',
      pickupInstructions: 'Collect at 10 Main Street',
      active: true,
      fee: 0,
      feeMinor: 0,
      countries: [],
      estimatedDays: '',
    },
  ]
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  await page.getByLabel('Shipping method', { exact: true }).selectOption('pickup')
  await expect(page.getByLabel('Address line 1', { exact: true })).toHaveCount(0)
  await page.getByLabel('Pickup name').fill('Buyer')
  await page.getByLabel('Pickup phone').fill('9876543210')
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  expect(state.orders[0].pickupContact).toEqual({ name: 'Buyer', phone: '9876543210' })
  await page.goto('/orders/order-001')
  await expect(page.getByText('Collect at 10 Main Street')).toBeVisible()
  await expect(page.getByText(/Reservation expires:/)).toBeVisible()
  state.orders[0].status = 'expired'
  await page.reload()
  await expect(
    page.getByText('This pending order expired. Its reserved stock has been released.'),
  ).toBeVisible()
  await expect(page.getByRole('button', { name: 'Cancel order', exact: true })).toHaveCount(0)
})

test('checkout rate limit shows retry guidance without losing the original intent', async ({
  page,
}) => {
  const state = await setup(page)
  state.cart = [{ productId: 'cup', quantity: 1 }]
  await login(page, '/checkout')
  await fillAddress(page)
  const attempts = []
  await page.route('http://127.0.0.1:8089/orders', async (route) => {
    if (route.request().method() !== 'POST') return route.fallback()
    attempts.push({
      key: route.request().headers()['idempotency-key'],
      body: route.request().postDataJSON(),
    })
    if (attempts.length === 1)
      return route.fulfill({
        status: 429,
        headers: { 'Retry-After': '6', 'Access-Control-Expose-Headers': 'Retry-After' },
        json: { code: 'RATE_LIMITED', error: 'Too many requests' },
      })
    return route.fallback()
  })
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('alert')).toContainText('Try again in 6 seconds')
  await expect(page.getByLabel('I have reviewed my items and total.')).not.toBeChecked()
  await page.getByLabel('I have reviewed my items and total.').check()
  await page.getByRole('button', { name: 'Confirm order' }).click()
  await expect(page.getByRole('heading', { name: 'Your order is in.' })).toBeVisible()
  expect(attempts[1]).toEqual(attempts[0])
})

test('admin can prepare and complete pickup without adding a delivery address', async ({
  page,
}) => {
  const state = await setup(page, { role: 'admin' })
  state.orders = [
    {
      id: 'pickup-admin',
      userId: 'test-user',
      status: 'paid',
      total: 650,
      createdAt: 1789128000,
      items: [{ productId: 'cup', name: 'Ceramic Cup', price: 650, quantity: 1 }],
      shippingMethod: { name: 'Pickup', kind: 'pickup', pickupInstructions: 'Store counter' },
      pickupContact: { name: 'Buyer', phone: '9876543210' },
      shippingAddress: null,
    },
  ]
  await login(page, '/orders/pickup-admin')
  await expect(page.getByRole('button', { name: 'Save order address' })).toHaveCount(0)
  await page.getByRole('button', { name: 'Mark ready for pickup' }).click()
  await page.getByRole('button', { name: 'Confirm status' }).click()
  await expect(page.getByText('Ready for pickup', { exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Mark collected' }).click()
  await page.getByRole('button', { name: 'Confirm status' }).click()
  await expect(page.getByText('Collected', { exact: true })).toBeVisible()
})

test('history pagination preserves loaded rows on failure and retries the same page', async ({
  page,
}) => {
  const state = await setup(page)
  state.notifications = Array.from({ length: 30 }, (_, i) => ({
    id: `note-${i}`,
    message: `Update number ${i}`,
    createdAt: 100 + i,
    orderId: 'example',
    read: false,
  }))
  let failNext = true
  const cursors = []
  await page.route('http://127.0.0.1:8089/notifications?**', async (route) => {
    const cursor = new URL(route.request().url()).searchParams.get('cursor')
    cursors.push(cursor)
    if (cursor && failNext) {
      failNext = false
      return route.fulfill({
        status: 503,
        json: { error: 'Temporary page failure' },
        headers: { 'Access-Control-Allow-Origin': '*' },
      })
    }
    return route.fallback()
  })
  await login(page, '/notifications')
  await expect(page.getByText('25 records loaded · More available')).toBeVisible()
  await expect(page.getByText('Update number 0', { exact: true })).toHaveCount(0)
  await page.getByRole('button', { name: 'Load more', exact: true }).click()
  await expect(page.getByText('Temporary page failure')).toBeVisible()
  await expect(page.getByText('Update number 29', { exact: false })).toBeVisible()
  await page.getByRole('button', { name: 'Retry loading more' }).click()
  await expect(page.getByText('30 records loaded')).toBeVisible()
  await expect(page.getByText('Update number 0', { exact: false })).toBeVisible()
  expect(cursors.filter(Boolean)).toEqual(['25', '25'])
  await expect(page.getByRole('button', { name: 'Load more', exact: true })).toHaveCount(0)
  await page.getByRole('button', { name: 'Refresh notifications' }).click()
  await expect(page.getByText('25 records loaded · More available')).toBeVisible()
})

test('audit search explicitly covers loaded records and expands with the next page', async ({
  page,
}) => {
  const state = await setup(page, { role: 'admin' })
  state.audits = Array.from({ length: 30 }, (_, i) => ({
    id: `audit-${i}`,
    actorId: 'test-user',
    action: 'product_updated',
    targetId: `product-${i}`,
    createdAt: 100 + i,
    changes: {},
  }))
  await login(page, '/admin/audit')
  await page.getByLabel('Search audit log').fill('product-0')
  await expect(page.getByText('No audit records found')).toBeVisible()
  await expect(page.getByText('Search applies to loaded records.', { exact: false })).toBeVisible()
  await page.getByRole('button', { name: 'Load more', exact: true }).click()
  await expect(page.getByRole('cell', { name: 'product-0', exact: true })).toBeVisible()
  await expect(page.getByText('30 records loaded')).toBeVisible()
})

test('preview is labelled on storefront and admin pages', async ({ page }) => {
  await setup(page, { role: 'admin' })
  await page.goto('/')
  await expect(page.getByRole('note')).toHaveText(
    'Demo store · Test orders only. No payments are collected.',
  )
  await login(page, '/admin')
  await expect(page.getByRole('note')).toHaveText(
    'Demo store · Test orders only. No payments are collected.',
  )
})
