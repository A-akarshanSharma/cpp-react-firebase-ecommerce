import { Link, useParams } from 'react-router-dom'
import { EmptyState, PageHeading } from '../components/common/UI'
const pages = {
  about: [
    'A little intention goes a long way.',
    'Studio Thread is a place for thoughtful finds and everyday inspiration. Explore our collection, read the details, and choose the pieces that feel like you.',
  ],
  support: [
    'Here to help.',
    'You can review and update items in your bag before ordering. Once you confirm, your order and its current status are available in Your orders. Open an order to view its delivery address and progress or cancel while it is pending. Payments and returns are arranged separately.',
  ],
  contact: [
    'Let’s stay in touch.',
    'Store contact details have not been published yet. Please check back for customer service information.',
  ],
  privacy: [
    'Your privacy matters.',
    'Sign-in is managed by Firebase Authentication. The store uses your account to maintain your bag, order history, and account role. Your delivery name, address and phone number are saved with each order. The store owner’s full privacy policy has not been published yet.',
  ],
  terms: [
    'The details, thoughtfully stated.',
    'Order confirmation creates a pending order. No online payment is collected. Full terms of sale, delivery, and returns will be published by the store owner before those services become available.',
  ],
  social: [
    'Follow the studio.',
    'Our social channels have not been published yet. In the meantime, explore what’s in the collection.',
  ],
}
export default function InfoPage({ page }) {
  const params = useParams(),
    key = page || params.page,
    content = pages[key]
  return (
    <div className="container section info-page">
      {content ? (
        <>
          <PageHeading eyebrow="STUDIO THREAD" title={content[0]} />
          <p className="info-copy">{content[1]}</p>
          <Link className="btn primary" to={key === 'support' ? '/orders' : '/shop'}>
            {key === 'support' ? 'View your orders' : 'Explore the collection'} →
          </Link>
        </>
      ) : (
        <EmptyState title="This page has wandered off" to="/" action="Back home">
          We couldn’t find the page you’re looking for.
        </EmptyState>
      )}
    </div>
  )
}
